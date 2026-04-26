#include <usb/hid.h>
#include <ps2kbd.h>
#include <kernel/terminal.h>

// Таблица конвертации USB usage -> PS/2 scancode (set 1)
static const u8 usb_to_ps2_scancode[0x70] = {
    0x00, // 0x00 Reserved
    0x29, // 0x01 Keyboard ErrorRollOver
    0x29, // 0x02 Keyboard POSTFail
    0x29, // 0x03 Keyboard ErrorUndefined
    0x1E, // 0x04 Keyboard a and A -> PS/2 0x1E
    0x30, // 0x05 Keyboard b and B -> 0x30
    0x2E, // 0x06 Keyboard c and C -> 0x2E
    0x20, // 0x07 Keyboard d and D -> 0x20
    0x12, // 0x08 Keyboard e and E -> 0x12
    0x21, // 0x09 Keyboard f and F -> 0x21
    0x22, // 0x0A Keyboard g and G -> 0x22
    0x23, // 0x0B Keyboard h and H -> 0x23
    0x17, // 0x0C Keyboard i and I -> 0x17
    0x24, // 0x0D Keyboard j and J -> 0x24
    0x25, // 0x0E Keyboard k and K -> 0x25
    0x26, // 0x0F Keyboard l and L -> 0x26
    0x32, // 0x10 Keyboard m and M -> 0x32
    0x31, // 0x11 Keyboard n and N -> 0x31
    0x18, // 0x12 Keyboard o and O -> 0x18
    0x19, // 0x13 Keyboard p and P -> 0x19
    0x10, // 0x14 Keyboard q and Q -> 0x10
    0x13, // 0x15 Keyboard r and R -> 0x13
    0x1F, // 0x16 Keyboard s and S -> 0x1F
    0x14, // 0x17 Keyboard t and T -> 0x14
    0x16, // 0x18 Keyboard u and U -> 0x16
    0x2F, // 0x19 Keyboard v and V -> 0x2F
    0x11, // 0x1A Keyboard w and W -> 0x11
    0x2D, // 0x1B Keyboard x and X -> 0x2D
    0x15, // 0x1C Keyboard y and Y -> 0x15
    0x2C, // 0x1D Keyboard z and Z -> 0x2C
    0x02, // 0x1E Keyboard 1 and ! -> 0x02
    0x03, // 0x1F Keyboard 2 and @ -> 0x03
    0x04, // 0x20 Keyboard 3 and # -> 0x04
    0x05, // 0x21 Keyboard 4 and $ -> 0x05
    0x06, // 0x22 Keyboard 5 and % -> 0x06
    0x07, // 0x23 Keyboard 6 and ^ -> 0x07
    0x08, // 0x24 Keyboard 7 and & -> 0x08
    0x09, // 0x25 Keyboard 8 and * -> 0x09
    0x0A, // 0x26 Keyboard 9 and ( -> 0x0A
    0x0B, // 0x27 Keyboard 0 and ) -> 0x0B
    0x1C, // 0x28 Keyboard Return (ENTER) -> 0x1C
    0x01, // 0x29 Keyboard ESCAPE -> 0x01
    0x0E, // 0x2A Keyboard DELETE (Backspace) -> 0x0E
    0x0F, // 0x2B Keyboard Tab -> 0x0F
    0x39, // 0x2C Keyboard Spacebar -> 0x39
    0x0C, // 0x2D Keyboard - and _ -> 0x0C
    0x0D, // 0x2E Keyboard = and + -> 0x0D
    0x1A, // 0x2F Keyboard [ and { -> 0x1A
    0x1B, // 0x30 Keyboard ] and } -> 0x1B
    0x2B, // 0x31 Keyboard \ and | -> 0x2B
    0x27, // 0x33 Keyboard ; and : -> 0x27
    0x28, // 0x34 Keyboard ' and " -> 0x28
    0x29, // 0x35 Keyboard ` and ~ -> 0x29
    0x33, // 0x36 Keyboard , and < -> 0x33
    0x34, // 0x37 Keyboard . and > -> 0x34
    0x35, // 0x38 Keyboard / and ? -> 0x35
    0x3A, // 0x39 Keyboard Caps Lock -> 0x3A
    0x3B, // 0x3A Keyboard F1 -> 0x3B
    0x3C, // 0x3B Keyboard F2 -> 0x3C
    0x3D, // 0x3C Keyboard F3 -> 0x3D
    0x3E, // 0x3D Keyboard F4 -> 0x3E
    0x3F, // 0x3E Keyboard F5 -> 0x3F
    0x40, // 0x3F Keyboard F6 -> 0x40
    0x41, // 0x40 Keyboard F7 -> 0x41
    0x42, // 0x41 Keyboard F8 -> 0x42
    0x43, // 0x42 Keyboard F9 -> 0x43
    0x44, // 0x43 Keyboard F10 -> 0x44
    0x57, // 0x44 Keyboard F11 -> 0x57
    0x58, // 0x45 Keyboard F12 -> 0x58
    0x46, // 0x46 Keyboard Print Screen -> 0x46 (needs special handling)
    0x47, // 0x47 Keyboard Scroll Lock -> 0x47
    0x45, // 0x48 Keyboard Pause -> 0x45
    0x52, // 0x49 Keyboard Insert -> 0x52
    0x47, // 0x4A Keyboard Home -> 0x47
    0x49, // 0x4B Keyboard Page Up -> 0x49
    0x53, // 0x4C Keyboard Delete -> 0x53
    0x4F, // 0x4D Keyboard End -> 0x4F
    0x51, // 0x4E Keyboard Page Down -> 0x51
    0x4B, // 0x4F Keyboard Right Arrow -> 0x4B
    0x50, // 0x50 Keyboard Left Arrow -> 0x50
    0x48, // 0x51 Keyboard Down Arrow -> 0x48
    0x52, // 0x52 Keyboard Up Arrow -> 0x52
    0x37, // 0x53 Keypad Num Lock -> 0x37
    0x4A, // 0x54 Keypad / -> 0x4A
    0x37, // 0x55 Keypad * -> 0x37
    0x4A, // 0x56 Keypad - -> 0x4A
    0x4E, // 0x57 Keypad + -> 0x4E
    0x1C, // 0x58 Keypad ENTER -> 0x1C
    0x4F, // 0x59 Keypad 1 and End -> 0x4F
    0x50, // 0x5A Keypad 2 and Down Arrow -> 0x50
    0x51, // 0x5B Keypad 3 and Page Down -> 0x51
    0x4B, // 0x5C Keypad 4 and Left Arrow -> 0x4B
    0x4C, // 0x5D Keypad 5 -> 0x4C
    0x4D, // 0x5E Keypad 6 and Right Arrow -> 0x4D
    0x47, // 0x5F Keypad 7 and Home -> 0x47
    0x48, // 0x60 Keypad 8 and Up Arrow -> 0x48
    0x49, // 0x61 Keypad 9 and Page Up -> 0x49
    0x52, // 0x62 Keypad 0 and Insert -> 0x52
    0x53, // 0x63 Keypad . and Delete -> 0x53
};

static u8 usb_to_ps2(u8 usage) {
    if (usage < sizeof(usb_to_ps2_scancode)) {
        return usb_to_ps2_scancode[usage];
    }
    return 0x29; // Error
}

static void hid_keyboard_input(hid_device_t *hid, u8 *data, u32 len) {
    (void)hid;
    
    if (len < 8) return;
    
    u8 modifiers = data[0];
    u8 reserved = data[1];
    (void)reserved;
    
    // Process modifier keys
    static u8 last_modifiers = 0;
    if (modifiers != last_modifiers) {
        u8 changed = modifiers ^ last_modifiers;
        
        if (changed & 0x01) { // Left Control
            u8 scancode = (modifiers & 0x01) ? 0x1D : 0x9D;
            kbd_push_raw_scancode(scancode);
        }
        if (changed & 0x02) { // Left Shift
            u8 scancode = (modifiers & 0x02) ? 0x2A : 0xAA;
            kbd_push_raw_scancode(scancode);
        }
        if (changed & 0x04) { // Left Alt
            u8 scancode = (modifiers & 0x04) ? 0x38 : 0xB8;
            kbd_push_raw_scancode(scancode);
        }
        if (changed & 0x08) { // Left GUI (Windows)
            u8 scancode = (modifiers & 0x08) ? 0x5B : 0xDB;
            kbd_push_raw_scancode(scancode);
        }
        if (changed & 0x10) { // Right Control
            u8 scancode = (modifiers & 0x10) ? 0x1D : 0x9D;
            kbd_push_raw_scancode(scancode);
        }
        if (changed & 0x20) { // Right Shift
            u8 scancode = (modifiers & 0x20) ? 0x36 : 0xB6;
            kbd_push_raw_scancode(scancode);
        }
        if (changed & 0x40) { // Right Alt (AltGr)
            u8 scancode = (modifiers & 0x40) ? 0x38 : 0xB8;
            kbd_push_raw_scancode(scancode);
        }
        if (changed & 0x80) { // Right GUI
            u8 scancode = (modifiers & 0x80) ? 0x5C : 0xDC;
            kbd_push_raw_scancode(scancode);
        }
        
        last_modifiers = modifiers;
    }
    
    // Process key presses (max 6 keys)
    static u8 last_keys[6] = {0,0,0,0,0,0};
    
    for (int i = 2; i < 8; i++) {
        u8 key = data[i];
        if (key == 0) continue;
        
        // Check if key was not pressed before
        bool was_pressed = false;
        for (int j = 0; j < 6; j++) {
            if (last_keys[j] == key) {
                was_pressed = true;
                break;
            }
        }
        
        if (!was_pressed) {
            u8 scancode = usb_to_ps2(key);
            if (scancode != 0x29) {
                kbd_push_raw_scancode(scancode);
                terminal_debug_printf("[USB-KBD] Key 0x%x -> PS/2 0x%x\n", key, scancode);
            }
        }
    }
    
    // Process key releases
    for (int i = 0; i < 6; i++) {
        u8 old_key = last_keys[i];
        if (old_key == 0) continue;
        
        bool still_pressed = false;
        for (int j = 2; j < 8; j++) {
            if (data[j] == old_key) {
                still_pressed = true;
                break;
            }
        }
        
        if (!still_pressed) {
            u8 scancode = usb_to_ps2(old_key);
            if (scancode != 0x29) {
                kbd_push_raw_scancode(scancode | 0x80); // Break code
            }
        }
    }
    
    memcpy(last_keys, data + 2, 6);
}

static int hid_keyboard_probe(hid_device_t *hid) {
    terminal_printf("[USB-KBD] Probing...\n");
    
    // Set input callback
    hid->input_report_callback = hid_keyboard_input;
    
    terminal_success_printf("[USB-KBD] Keyboard ready\n");
    return 0;
}

static hid_driver_t g_keyboard_driver = {
    .name = "hid-keyboard",
    .vendor_id = 0,
    .product_id = 0,
    .usage_page = 0x01,
    .usage = 0x06,  // Keyboard usage
    .probe = hid_keyboard_probe,
    .disconnect = NULL,
};

void hid_keyboard_init(void) {
    hid_register_driver(&g_keyboard_driver);
}