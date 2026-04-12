#include <ps2kbd.h>
#include <asm/io.h>
#include <ioapic.h>
#include <kernel/types.h>
#include <kernel/driver.h>
#include <kernel/terminal.h>

#define INTERNAL_SPACE 0x01
#define KEYBOARD_PORT 0x60

bool shift_down = false;
bool caps_lock = false;
bool ctrl_down = false;
volatile bool input_waiting = false;
volatile bool can_type = true;

/*
 * Ring buffer
 */
char kbd_buf[KBD_BUF_SIZE];
volatile int kbd_head = 0; /*
 * room for the next push
 */
volatile int kbd_tail = 0; /*
 * reading space
 */

static int ps2kbd_probe(driver_t* drv) {
    (void)drv;
    return 0; // There is always one on laptops
}

driver_t g_ps2_kbd_driver = {
    .name = "ps2_kbd",
    .desc = "Personal System/2 Keyboard Driver",
    .critical_level = DRIVER_CRITICAL_3,
    .probe = ps2kbd_probe,
    .init = NULL,
    .remove = NULL,
};

// Table 0–255, all unused elements = 0
const char scancode_to_ascii[256] = {
    [KEY_A] = 'a',
    [KEY_B] = 'b',
    [KEY_C] = 'c',
    [KEY_D] = 'd',
    [KEY_E] = 'e',
    [KEY_F] = 'f',
    [KEY_G] = 'g',
    [KEY_H] = 'h',
    [KEY_I] = 'i',
    [KEY_J] = 'j',
    [KEY_K] = 'k',
    [KEY_L] = 'l',
    [KEY_M] = 'm',
    [KEY_N] = 'n',
    [KEY_O] = 'o',
    [KEY_P] = 'p',
    [KEY_Q] = 'q',
    [KEY_R] = 'r',
    [KEY_S] = 's',
    [KEY_T] = 't',
    [KEY_U] = 'u',
    [KEY_V] = 'v',
    [KEY_W] = 'w',
    [KEY_X] = 'x',
    [KEY_Y] = 'y',
    [KEY_Z] = 'z',

    [KEY_1] = '1',
    [KEY_2] = '2',
    [KEY_3] = '3',
    [KEY_4] = '4',
    [KEY_5] = '5',
    [KEY_6] = '6',
    [KEY_7] = '7',
    [KEY_8] = '8',
    [KEY_9] = '9',
    [KEY_0] = '0',

    [KEY_MINUS] = '-',
    [KEY_EQUAL] = '=',
    [KEY_SQUARE_OPEN_BRACKET] = '[',
    [KEY_SQUARE_CLOSE_BRACKET] = ']',
    [KEY_SEMICOLON] = ';',
    [KEY_BACKSLASH] = '\\',
    [KEY_COMMA] = ',',
    [KEY_DOT] = '.',
    [KEY_FORESLHASH] = '/',
    [KEY_APOSTROPHE] = '\'',
    [KEY_GRAVE] = '`',

    [KEY_SPACE] = ' ',
    [KEY_ENTER] = '\n',
    [KEY_TAB] = '\t',
    [KEY_BACKSPACE] = '\b',
};

const char scancode_to_ascii_shifted[256] = {
    [KEY_A] = 'A',
    [KEY_B] = 'B',
    [KEY_C] = 'C',
    [KEY_D] = 'D',
    [KEY_E] = 'E',
    [KEY_F] = 'F',
    [KEY_G] = 'G',
    [KEY_H] = 'H',
    [KEY_I] = 'I',
    [KEY_J] = 'J',
    [KEY_K] = 'K',
    [KEY_L] = 'L',
    [KEY_M] = 'M',
    [KEY_N] = 'N',
    [KEY_O] = 'O',
    [KEY_P] = 'P',
    [KEY_Q] = 'Q',
    [KEY_R] = 'R',
    [KEY_S] = 'S',
    [KEY_T] = 'T',
    [KEY_U] = 'U',
    [KEY_V] = 'V',
    [KEY_W] = 'W',
    [KEY_X] = 'X',
    [KEY_Y] = 'Y',
    [KEY_Z] = 'Z',

    [KEY_1] = '!',
    [KEY_2] = '@',
    [KEY_3] = '#',
    [KEY_4] = '$',
    [KEY_5] = '%',
    [KEY_6] = '^',
    [KEY_7] = '&',
    [KEY_8] = '*',
    [KEY_9] = '(',
    [KEY_0] = ')',

    [KEY_MINUS] = '_',
    [KEY_EQUAL] = '+',
    [KEY_SQUARE_OPEN_BRACKET] = '{',
    [KEY_SQUARE_CLOSE_BRACKET] = '}',
    [KEY_SEMICOLON] = ':',
    [KEY_BACKSLASH] = '|',
    [KEY_COMMA] = '<',
    [KEY_DOT] = '>',
    [KEY_FORESLHASH] = '?',
    [KEY_APOSTROPHE] = '"',
    [KEY_GRAVE] = '~',

    [KEY_SPACE] = ' ',
    [KEY_ENTER] = '\n',
    [KEY_TAB] = '\t',
    [KEY_BACKSPACE] = '\b',
};

char my_toupper(char c)
{
    if (c >= 'a' && c <= 'z')
    {
        return c - 32;
    }
    return c;
}

bool is_alpha(u8 scancode)
{
    switch (scancode)
    {
    case KEY_A:
    case KEY_B:
    case KEY_C:
    case KEY_D:
    case KEY_E:
    case KEY_F:
    case KEY_G:
    case KEY_H:
    case KEY_I:
    case KEY_J:
    case KEY_K:
    case KEY_L:
    case KEY_M:
    case KEY_N:
    case KEY_O:
    case KEY_P:
    case KEY_Q:
    case KEY_R:
    case KEY_S:
    case KEY_T:
    case KEY_U:
    case KEY_V:
    case KEY_W:
    case KEY_X:
    case KEY_Y:
    case KEY_Z:
        return true;
    default:
        return false;
    }
}

// Convert scancode to ASCII (or 0 if there is no match)
char get_ascii_char(u8 scancode)
{
    if (is_alpha(scancode))
    {
        bool upper = shift_down ^ caps_lock;
        char base = scancode_to_ascii[scancode]; // 'a'–'z'
        return upper ? my_toupper(base) : base;
    }

    if (shift_down)
    {
        return scancode_to_ascii_shifted[scancode];
    }
    else
    {
        return scancode_to_ascii[scancode];
    }
}

/*
 * Simple helpers for atomicity: saving/restoring flags
 */
static inline unsigned long irq_save_flags(void)
{
    unsigned long flags;
    __asm__ volatile("pushf; pop %0; cli" : "=g"(flags)::"memory");
    return flags;
}

static inline void irq_restore_flags(unsigned long flags)
{
    __asm__ volatile("push %0; popf" ::"g"(flags) : "memory", "cc");
}

/*
 * Called from the ISR (keyboard_handler) Appends ASCII to the buffer (if not full).
 */
void kbd_buffer_push(char c)
{
    unsigned long flags = irq_save_flags(); /*
 * disable interrupts for a short time
 */
    int next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) /*
 * if not complete
 */
    {
        kbd_buf[kbd_head] = c;
        kbd_head = next;
    }
    else
    {
        /*
 * buffer full - losing character (alternative: overwrite oldest)
 */
    }
    irq_restore_flags(flags);
}

/*
 * Takes a character from the buffer without blocking Returns -1 if empty.
 */
char kbd_getchar(void)
{
    unsigned long flags = irq_save_flags();

    if (!can_type)
    {
        irq_restore_flags(flags);
        return -1; // Return -1 if output is disabled.
    }

    if (kbd_head == kbd_tail)
    {
        irq_restore_flags(flags);
        return -1; /*
 * empty
 */
    }
    char c = (char)kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    irq_restore_flags(flags);
    return c;
}

/*
 * Modified keyboard handler - instead of printing, we push the character to the buffer.
 */
void keyboard_handler(void)
{
    u8 code = inb(KEYBOARD_PORT);

    //Checking the Break code (high bit = 1)
    bool released = code & 0x80;
    u8 key = code & 0x7F;

    //Processing Ctrl
    if (key == KEY_LCONTROL || key == KEY_RCONTROL)
    {
        ctrl_down = !released;
        ioapic_eoi(1);
        return;
    }

    if (!released && ctrl_down && key == KEY_C)
    {
        kbd_buffer_push(0x03); //ASCII code for Ctrl+C
        ioapic_eoi(1);
        return;
    }

    //Shift Processing
    if (key == KEY_LSHIFT || key == KEY_RSHIFT)
    {
        shift_down = !released;
        ioapic_eoi(1);
        return;
    }
    
    //Handling CapsLock
    if (key == KEY_CAPSLOCK && !released)
    {
        caps_lock = !caps_lock;
        ioapic_eoi(1);
        return;
    }

    //If the key is released - only EOI
    if (released)
    {
        ioapic_eoi(1);
        return;
    }

    // Processing pressed keys
    char ch = get_ascii_char(key);
    
    if (ch) {
        if (g_terminal.prompt_enabled) {
            terminal_handle_input(ch);
        } else {
            kbd_buffer_push(ch);
        }
    }

    ioapic_eoi(1);
}
