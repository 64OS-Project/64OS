#include <kernel/terminal.h>
#include <kernel/timer.h>
#include <ps2kbd.h>
#include <libk/string.h>
#include <mm/heap.h>
#include <asm/cpu.h>
#include <fs/vfs.h>

/*
 * Global terminal
 */
terminal_t g_terminal;

/*
 * Maximum lines in buffer (default 1000)
 */
#define DEFAULT_MAX_LINES 1000
#define SCROLLBAR_WIDTH 2

static char line_buffer[1024];
static u32 line_pos = 0;

extern vfs_inode_t *current_dir;
extern char current_path[PATH_MAX];
extern vfs_inode_t *fs_root;

/*
 * ============================================================================== Internal functions =======================================================================================
 */
static int term_get_wrapped_line(term_line_t* line, int part, char* out, int max_len) {
    int max_chars = (fb_get_width() - SCROLLBAR_WIDTH - 4) / FONT_WIDTH;
    if (max_chars <= 0) max_chars = 80;
    
    int len = strlen(line->text);
    int start = part * max_chars;
    
    if (start >= len) return -1;
    
    int chunk = (len - start < max_chars) ? (len - start) : max_chars;
    strncpy(out, line->text + start, chunk);
    out[chunk] = '\0';
    
    return 0;
}

void terminal_generate_prompt(char *buffer, int buf_size) {
    if (!buffer || buf_size <= 0) return;
    
    char path[PATH_MAX];
    
    /*
 * Getting the current path
 */
    if (current_dir && current_dir != fs_root) {
        if (vfs_build_path(current_dir, path, PATH_MAX) == 0) {
            /*
 * If the path is not "/", add it
 */
            snprintf(buffer, buf_size, "%s> ", path);
        } else {
            snprintf(buffer, buf_size, "/> ");
        }
    } else {
        /*
 * Root directory
 */
        snprintf(buffer, buf_size, "/> ");
    }
}

/*
 * Counting how many parts a string has
 */
static int term_line_parts(term_line_t* line) {
    int max_chars = (fb_get_width() - SCROLLBAR_WIDTH - 4) / FONT_WIDTH;
    if (max_chars <= 0) max_chars = 80;
    
    int len = strlen(line->text);
    return (len + max_chars - 1) / max_chars;
}


/*
 * Adding a line to the scroll buffer
 */
void term_add_line(const char* text, fb_color_t color) {
    term_line_t* new_line = (term_line_t*)malloc(sizeof(term_line_t));
    if (!new_line) return;
    
    new_line->text = (char*)malloc(strlen(text) + 1);
    if (!new_line->text) {
        free(new_line);
        return;
    }
    new_line->color = color;
    strcpy(new_line->text, text);
    new_line->next = NULL;
    new_line->prev = g_terminal.lines_tail;
    
    if (g_terminal.lines_tail) {
        g_terminal.lines_tail->next = new_line;
    } else {
        g_terminal.lines_head = new_line;
    }
    g_terminal.lines_tail = new_line;
    g_terminal.total_lines++;
    
    /*
 * Buffer size limit
 */
    while (g_terminal.total_lines > g_terminal.max_buffer_lines) {
        term_line_t* old = g_terminal.lines_head;
        g_terminal.lines_head = old->next;
        if (g_terminal.lines_head) {
            g_terminal.lines_head->prev = NULL;
        }
        free(old->text);
        free(old);
        g_terminal.total_lines--;
    }
    
    /*
 * Auto scroll down
 */
    if (g_terminal.auto_scroll && g_terminal.scroll_offset == 0) {
        g_terminal.current_line = g_terminal.lines_tail;
    }
}

/*
 * Retrieving a string from a buffer by index (0 = oldest)
 */
static term_line_t* term_get_line_ptr(i32 index) {
    if (index < 0 || index >= g_terminal.total_lines) return NULL;
    
    term_line_t* line = g_terminal.lines_head;
    for (i32 i = 0; i < index && line; i++) {
        line = line->next;
    }
    return line;
}

/*
 * Calculation of visible lines
 */
static void term_calc_visible_lines(void) {
    g_terminal.visible_lines = fb_get_height() / (FONT_HEIGHT + 2);
    /*
 * Leave room for prompt and scrollbar
 */
    if (g_terminal.prompt_enabled && g_terminal.mode == TERM_MODE_PROMPT) {
        g_terminal.visible_lines -= 1;  /*
 * Prompt string + empty string
 */
    }
}

/*
 * Drawing a scrollbar
 */
static void term_draw_scrollbar(void) {
    if (g_terminal.total_lines <= g_terminal.visible_lines) return;
    
    i32 screen_height = fb_get_height();
    i32 bar_height = screen_height * g_terminal.visible_lines / g_terminal.total_lines;
    if (bar_height < 10) bar_height = 10;
    
    i32 scroll_pos = g_terminal.total_lines - g_terminal.visible_lines;
    i32 current_scroll = scroll_pos - g_terminal.scroll_offset;
    if (current_scroll < 0) current_scroll = 0;
    
    i32 bar_y = (screen_height - bar_height) * current_scroll / scroll_pos;
    if (bar_y < 0) bar_y = 0;
    if (bar_y + bar_height > screen_height) bar_y = screen_height - bar_height;
    
    i32 x = fb_get_width() - SCROLLBAR_WIDTH;
    
    /*
 * Drawing the background of the scrollbar
 */
    fb_fill_rect(x, 0, SCROLLBAR_WIDTH, screen_height, FB_DARK_GRAY);
    
    /*
 * Drawing a slider
 */
    fb_fill_rect(x, bar_y, SCROLLBAR_WIDTH, bar_height, g_terminal.scrollbar_color);
}

static void term_draw_line(i32 screen_line, const char* text, fb_color_t fg, fb_color_t bg) {
    i32 y = screen_line * (FONT_HEIGHT + 2);
    fb_draw_string(text, 2, y, fg, bg);
}

/*
 * Rendering a prompt
 */
static void term_draw_prompt(void) {
    i32 y = (g_terminal.visible_lines) * (FONT_HEIGHT + 2);
    
    /*
 * Clearing the prompt area
 */
    fb_fill_rect(0, y, fb_get_width(), FONT_HEIGHT + 4, g_terminal.color_bg);
    
    /*
 * Generating a dynamic prompt
 */
    char prompt_text[PATH_MAX + 32];
    terminal_generate_prompt(prompt_text, sizeof(prompt_text));
    
    /*
 * Adding the current input
 */
    char full_text[PATH_MAX + 256];
    snprintf(full_text, sizeof(full_text), "%s%s", prompt_text, g_terminal.input_buffer);
    
    /*
 * Draw prompt + input
 */
    fb_draw_string(full_text, 2, y, g_terminal.prompt_color, g_terminal.color_bg);
}

/*
 * ================================================================================ Public functions ======================================================================================
 */

void terminal_init(void) {
    memset(&g_terminal, 0, sizeof(terminal_t));
    
    g_terminal.mode = TERM_MODE_NORMAL;
    g_terminal.prompt_enabled = false;
    g_terminal.input_enabled = true;
    g_terminal.cursor_x = 0;
    g_terminal.cursor_y = 0;
    g_terminal.scroll_offset = 0;
    g_terminal.total_lines = 0;
    g_terminal.auto_scroll = true;
    g_terminal.max_buffer_lines = DEFAULT_MAX_LINES;
    g_terminal.lines_head = NULL;
    g_terminal.lines_tail = NULL;
    g_terminal.current_line = NULL;
    g_terminal.input_pos = 0;
    memset(g_terminal.input_buffer, 0, sizeof(g_terminal.input_buffer));
    
    /*
 * Colors
 */
    g_terminal.color_fg = FB_WHITE;
    g_terminal.color_bg = FB_BLACK;
    g_terminal.prompt_color = FB_GREEN;
    g_terminal.scrollbar_color = FB_GRAY;

    g_terminal.info_color = FB_RGB(0, 100, 255);      /*
 * Blue
 */
    g_terminal.warn_color = FB_RGB(255, 165, 0);      /*
 * Orange
 */
    g_terminal.error_color = FB_RGB(255, 50, 50);     /*
 * Bright red
 */
    g_terminal.debug_color = FB_RGB(128, 128, 128);   /*
 * Grey
 */
    
    term_calc_visible_lines();
}

void terminal_set_mode(term_mode_t mode) {
    g_terminal.mode = mode;
    terminal_refresh();
}

void terminal_enable_prompt(bool enable) {
    g_terminal.prompt_enabled = enable;
    if (enable) {
        g_terminal.mode = TERM_MODE_PROMPT;
        g_terminal.input_pos = 0;
        memset(g_terminal.input_buffer, 0, sizeof(g_terminal.input_buffer));
    } else {
        g_terminal.mode = TERM_MODE_NORMAL;
    }
    terminal_refresh();
}

void terminal_prompt_toggle(void) {
    terminal_enable_prompt(!g_terminal.prompt_enabled);
}

void terminal_clear_screen(void) {
    //Clearing the line buffer
    term_line_t* line = g_terminal.lines_head;
    while (line) {
        term_line_t* next = line->next;
        free(line->text);
        free(line);
        line = next;
    }
    g_terminal.lines_head = NULL;
    g_terminal.lines_tail = NULL;
    g_terminal.total_lines = 0;
    g_terminal.scroll_offset = 0;
    g_terminal.current_line = NULL;
    
    //Redraw the output area (make it empty)
    i32 content_height = g_terminal.visible_lines * (FONT_HEIGHT + 2);
    fb_fill_rect(0, 0, fb_get_width(), content_height, g_terminal.color_bg);
    
    //Redrawing the prompt
    if (g_terminal.prompt_enabled && g_terminal.mode == TERM_MODE_PROMPT) {
        terminal_update_prompt_line();
    }
}

void terminal_clear_buffer(void) {
    term_line_t* line = g_terminal.lines_head;
    while (line) {
        term_line_t* next = line->next;
        free(line->text);
        free(line);
        line = next;
    }
    g_terminal.lines_head = NULL;
    g_terminal.lines_tail = NULL;
    g_terminal.total_lines = 0;
    g_terminal.scroll_offset = 0;
    g_terminal.current_line = NULL;
    terminal_refresh();
}

void terminal_scroll_up(i32 lines) {
    if (g_terminal.total_lines <= g_terminal.visible_lines) return;
    
    i32 max_scroll = g_terminal.total_lines - g_terminal.visible_lines;
    g_terminal.scroll_offset += lines;
    if (g_terminal.scroll_offset > max_scroll) {
        g_terminal.scroll_offset = max_scroll;
    }
    g_terminal.auto_scroll = (g_terminal.scroll_offset == 0);
    if (g_terminal.auto_scroll) {
        g_terminal.cursor_y = g_terminal.visible_lines - 1;
        if (g_terminal.prompt_enabled && g_terminal.mode == TERM_MODE_PROMPT) {
            g_terminal.cursor_y--;
        }
    }
    terminal_refresh();
}

void terminal_scroll_down(i32 lines) {
    g_terminal.scroll_offset -= lines;
    if (g_terminal.scroll_offset < 0) {
        g_terminal.scroll_offset = 0;
    }
    g_terminal.auto_scroll = (g_terminal.scroll_offset == 0);
    terminal_refresh();
}

void terminal_scroll_to_top(void) {
    terminal_scroll_up(g_terminal.total_lines);
}

void terminal_scroll_to_bottom(void) {
    terminal_scroll_down(g_terminal.total_lines);
}

void terminal_set_auto_scroll(bool enable) {
    g_terminal.auto_scroll = enable;
    if (enable) {
        terminal_scroll_to_bottom();
    }
}

void terminal_print(const char* str) {
    if (!str) return;
    
    char buffer[1024];
    const char* p = str;
    const char* line_start = str;
    
    while (*p) {
        if (*p == '\n' || *p == '\r') {
            /*
 * Copy the line
 */
            sz len = p - line_start;
            if (len > 0) {
                strncpy(buffer, line_start, len);
                buffer[len] = '\0';
                term_add_line(buffer, g_terminal.color_fg);
            } else {
                term_add_line("", g_terminal.color_fg);
            }
            line_start = p + 1;
        }
        p++;
    }
    
    /*
 * Last line without translation
 */
    if (line_start < p) {
        term_add_line(line_start, g_terminal.color_fg);
    }
    
    terminal_refresh();
}

void terminal_print_color(const char* str, fb_color_t fg, fb_color_t bg) {
    if (!str) return;
    
    //Temporarily saving colors
    fb_color_t old_fg = g_terminal.color_fg;
    fb_color_t old_bg = g_terminal.color_bg;
    
    g_terminal.color_fg = fg;
    g_terminal.color_bg = bg;
    
    //Output the line
    terminal_print(str);
    
    //Restoring colors
    g_terminal.color_fg = old_fg;
    g_terminal.color_bg = old_bg;
}

void terminal_printf(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    terminal_print(buffer);
}

void terminal_putchar(char c) {
    char str[2] = {c, '\0'};
    
    //Get the current cursor position on the screen
    i32 y = g_terminal.cursor_y * (FONT_HEIGHT + 2);
    i32 x = 2 + g_terminal.cursor_x * FONT_WIDTH;
    
    if (c == '\n') {
        //New line
        g_terminal.cursor_x = 0;
        g_terminal.cursor_y++;
        
        //If you have reached the bottom of the output area (not counting the prompt line)
        i32 max_y = g_terminal.visible_lines;
        if (g_terminal.prompt_enabled && g_terminal.mode == TERM_MODE_PROMPT) {
            max_y--;  //The last line is a prompt
        }
        
        if (g_terminal.cursor_y >= max_y) {
            g_terminal.cursor_y = max_y - 1;
            //Scroll the screen up 1 line
            terminal_scroll_up(1);
        }
        terminal_refresh();
    } 
    else if (c == '\b') {
        //Backspace
        if (g_terminal.cursor_x > 0) {
            g_terminal.cursor_x--;
            //Recalculating the coordinates
            y = g_terminal.cursor_y * (FONT_HEIGHT + 2);
            x = 2 + g_terminal.cursor_x * FONT_WIDTH;
            fb_draw_string(" ", x, y, g_terminal.color_bg, g_terminal.color_bg);
        }
    } 
    else if (c >= 0x20 && c < 0x7F) {
        //Regular symbol
        fb_draw_string(str, x, y, g_terminal.color_fg, g_terminal.color_bg);
        g_terminal.cursor_x++;
        
        //If the line is full
        i32 max_x = (fb_get_width() - SCROLLBAR_WIDTH - 4) / FONT_WIDTH;
        if (g_terminal.cursor_x >= max_x) {
            terminal_putchar('\n');
        }
    }
}

void terminal_newline(void) {
    terminal_print("\n");
}

void terminal_update_prompt_line(void) {
    if (!fb_is_initialized()) return;
    if (!g_terminal.prompt_enabled || g_terminal.mode != TERM_MODE_PROMPT) return;
    
    i32 y = (g_terminal.visible_lines) * (FONT_HEIGHT + 2);
    
    //We clear only the prompt line
    fb_fill_rect(0, y, fb_get_width(), FONT_HEIGHT + 4, g_terminal.color_bg);
    
    //Generating a dynamic prompt with the current path
    char prompt_text[PATH_MAX + 32];
    terminal_generate_prompt(prompt_text, sizeof(prompt_text));
    
    //Drawing a prompt with the current input
    char full_text[PATH_MAX + 256];
    snprintf(full_text, sizeof(full_text), "%s%s", prompt_text, g_terminal.input_buffer);
    fb_draw_string(full_text, 2, y, g_terminal.prompt_color, g_terminal.color_bg);
}

char* terminal_getline(void) {
    /*
 * Blocking input - wait for Enter
 */
    g_terminal.input_enabled = true;
    g_terminal.input_pos = 0;
    memset(g_terminal.input_buffer, 0, sizeof(g_terminal.input_buffer));
    
    while (g_terminal.input_pos == 0 || 
           (g_terminal.input_buffer[g_terminal.input_pos - 1] != '\n' &&
            g_terminal.input_buffer[g_terminal.input_pos - 1] != '\r')) {
        asm volatile("hlt");
    }
    
    return g_terminal.input_buffer;
}

bool terminal_input_available(void) {
    return g_terminal.input_pos > 0;
}

i32 terminal_get_scroll_position(void) {
    return g_terminal.scroll_offset;
}

void terminal_refresh(void) {
    if (!fb_is_initialized()) return;
    
    term_calc_visible_lines();
    
    i32 content_height = g_terminal.visible_lines * (FONT_HEIGHT + 2);
    fb_fill_rect(0, 0, fb_get_width(), content_height, g_terminal.color_bg);
    
    //We count how many “parts” of lines are in the buffer
    int total_parts = 0;
    term_line_t* tmp = g_terminal.lines_head;
    while (tmp) {
        total_parts += term_line_parts(tmp);
        tmp = tmp->next;
    }
    
    //Determine which part to start displaying from
    int start_part = 0;
    int visible_parts = g_terminal.visible_lines;
    
    if (g_terminal.scroll_offset > 0) {
        start_part = g_terminal.scroll_offset;
    } else {
        start_part = total_parts - visible_parts;
        if (start_part < 0) start_part = 0;
    }
    
    int current_part = 0;
    int drawn = 0;
    term_line_t* line = g_terminal.lines_head;
    
    while (line && drawn < visible_parts) {
        int parts = term_line_parts(line);
        
        for (int p = 0; p < parts && drawn < visible_parts; p++) {
            if (current_part >= start_part) {
                char buffer[256];
                term_get_wrapped_line(line, p, buffer, sizeof(buffer));
                term_draw_line(drawn, buffer, line->color, g_terminal.color_bg);
                drawn++;
            }
            current_part++;
        }
        line = line->next;
    }
    
    term_draw_scrollbar();
    
    if (g_terminal.prompt_enabled && g_terminal.mode == TERM_MODE_PROMPT) {
        term_draw_prompt();
    }
}

void terminal_show_scrollbar(bool show) {
    /*
 * We always show the scrollbar if necessary
 */
    (void)show;
}

void terminal_info_printf(const char* format, ...) {
    char buffer[1024];
    va_list args;
    
    /*
 * Format the message
 */
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    /*
 * Add the prefix [INFO] and display it in blue
 */
    char colored_msg[1100];
    snprintf(colored_msg, sizeof(colored_msg), "%s", buffer);
    
    /*
 * Save to buffer with color
 */
    terminal_print_color(colored_msg, g_terminal.info_color, g_terminal.color_bg);
}

void terminal_warn_printf(const char* format, ...) {
    char buffer[1024];
    va_list args;
    
    /*
 * Format the message
 */
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    /*
 * Add the prefix [WARN] and display it in orange
 */
    char colored_msg[1100];
    snprintf(colored_msg, sizeof(colored_msg), "%s", buffer);
    
    /*
 * Save to buffer with color
 */
    terminal_print_color(colored_msg, g_terminal.warn_color, g_terminal.color_bg);
}

void terminal_error_printf(const char* format, ...) {
    char buffer[1024];
    va_list args;
    
    /*
 * Format the message
 */
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    /*
 * Add the prefix [ERROR] and display it in red
 */
    char colored_msg[1100];
    snprintf(colored_msg, sizeof(colored_msg), "%s", buffer);
    
    /*
 * Save to buffer with color
 */
    terminal_print_color(colored_msg, g_terminal.error_color, g_terminal.color_bg);
}

void terminal_debug_printf(const char* format, ...) {
    char buffer[1024];
    va_list args;
    
    /*
 * Format the message
 */
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    /*
 * Add the prefix [DEBUG] and display it in gray
 */
    char colored_msg[1100];
    snprintf(colored_msg, sizeof(colored_msg), "%s", buffer);
    
    /*
 * Save to buffer with color
 */
    terminal_print_color(colored_msg, g_terminal.debug_color, g_terminal.color_bg);
}

void terminal_success_printf(const char* format, ...) {
    char buffer[1024];
    va_list args;
    
    /*
 * Format the message
 */
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    /*
 * Add the prefix [OK] and display it in green
 */
    char colored_msg[1100];
    snprintf(colored_msg, sizeof(colored_msg), "%s", buffer);
    
    /*
 * Use green color (can be added to the terminal_t ​​structure)
 */
    fb_color_t green = FB_RGB(0, 255, 0);
    terminal_print_color(colored_msg, green, g_terminal.color_bg);
}

void terminal_update_cursor(void) {
    if (!fb_is_initialized()) return;
    if (!g_terminal.prompt_enabled || g_terminal.mode != TERM_MODE_PROMPT) return;
    
    static bool cursor_visible = true;
    cursor_visible = !cursor_visible;
    
    i32 y = (g_terminal.visible_lines) * (FONT_HEIGHT + 2);
    
    /*
 * Getting the length of the prompt for the correct cursor position
 */
    char prompt_text[PATH_MAX + 32];
    terminal_generate_prompt(prompt_text, sizeof(prompt_text));
    int prompt_len = strlen(prompt_text);
    
    /*
 * Cursor position = prompt length + input position
 */
    i32 cursor_x = 2 + (prompt_len + g_terminal.cursor_pos) * FONT_WIDTH;
    
    if (cursor_visible) {
        fb_draw_string("_", cursor_x, y, g_terminal.prompt_color, g_terminal.color_bg);
    } else {
        /*
 * Erase the cursor - draw a character under it or a space
 */
        char c = ' ';
        if (g_terminal.cursor_pos < g_terminal.input_pos) {
            c = g_terminal.input_buffer[g_terminal.cursor_pos];
        }
        char str[2] = {c, '\0'};
        fb_draw_string(str, cursor_x, y, g_terminal.prompt_color, g_terminal.color_bg);
    }
}

//Entering a string with a custom prompt
char* terminal_input(const char *prompt) {
    static char buffer[256];
    int pos = 0;
    
    //Saving the state
    bool old_prompt_enabled = g_terminal.prompt_enabled;
    term_mode_t old_mode = g_terminal.mode;
    
    //Temporarily disable prompt
    g_terminal.prompt_enabled = false;
    g_terminal.mode = TERM_MODE_NORMAL;

    terminal_printf("\n");
    
    //Place the cursor in the correct place (after the history)
    g_terminal.cursor_x = 0;
    g_terminal.cursor_y = g_terminal.visible_lines - 1;
    
    //We display an invitation
    if (prompt) {
        terminal_print_nn(prompt);
    }
    
    //Enter
    while (pos < sizeof(buffer) - 1) {
        char c = kbd_getchar();
        if (c == -1) continue;
            
        if (c == '\n' || c == '\r') {
            buffer[pos] = '\0';
            terminal_printf("\n");  //Adding a line to history
            break;
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                terminal_print_nn("\b \b");
            }
        } else if (c >= 0x20 && c < 0x7F) {
            buffer[pos++] = c;
            char str[2] = {c, '\0'};
            terminal_print_nn(str);
        }
    }
    
    //Restoring the mode
    g_terminal.prompt_enabled = old_prompt_enabled;
    g_terminal.mode = old_mode;
    
    //Update the prompt if necessary
    if (old_prompt_enabled) {
        terminal_update_prompt_line();
    }
    
    return buffer;
}

//Output without line feed and without adding to history
void terminal_print_nn(const char* str) {
    if (!str) return;
    
    //Just display it on the screen
    int y = g_terminal.cursor_y * (FONT_HEIGHT + 2);
    int x = 2 + g_terminal.cursor_x * FONT_WIDTH;
    fb_draw_string(str, x, y, g_terminal.color_fg, g_terminal.color_bg);
    g_terminal.cursor_x += strlen(str);
}

void terminal_printf_nn(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    terminal_print_nn(buffer);
}
