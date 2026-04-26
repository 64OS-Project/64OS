#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

#include <kernel/types.h>
#include <fb.h>

/*
 * Terminal Modes
 */
typedef enum {
    TERM_MODE_NORMAL,     /*
 * Normal mode - message output
 */
    TERM_MODE_PROMPT,     /*
 * Prompt mode
 */
    TERM_MODE_DEBUG       /*
 * Debug mode with detailed output
 */
} term_mode_t;

/*
 * String in scroll buffer
 */
typedef struct term_line {
    char* text;                 /*
 * Line text
 */
    fb_color_t color;
    struct term_line* next;     /*
 * Next line
 */
    struct term_line* prev;     /*
 * Previous line
 */
} term_line_t;

/*
 * Terminal structure
 */
typedef struct {
    term_mode_t mode;           /*
 * Current mode
 */
    bool prompt_enabled;        /*
 * Is prompt enabled?
 */
    bool input_enabled;         /*
 * Is input allowed?
 */
    
    /*
 * Cursor position on screen
 */
    i32 cursor_x;
    i32 cursor_y;
    i32 cursor_pos;
    
    /*
 * Position in scroll buffer
 */
    i32 scroll_offset;          /*
 * Scroll offset (0 = bottom)
 */
    i32 total_lines;            /*
 * Total lines in buffer
 */
    i32 visible_lines;          /*
 * Visible lines on screen
 */
    
    /*
 * Scroll buffer (doubly linked list)
 */
    term_line_t* lines_head;    /*
 * First line (oldest)
 */
    term_line_t* lines_tail;    /*
 * Last line (newest)
 */
    term_line_t* current_line;  /*
 * Current scroll position
 */
    
    /*
 * Input buffer
 */
    char input_buffer[256];
    u32 input_pos;
    
    /*
 * Settings
 */
    bool auto_scroll;           /*
 * Automatic scroll to bottom
 */
    u32 max_buffer_lines;       /*
 * Maximum lines in buffer
 */
    
    /*
 * Colors
 */
    fb_color_t color_fg;
    fb_color_t color_bg;
    fb_color_t prompt_color;
    fb_color_t scrollbar_color;

    fb_color_t info_color;      /*
 * Blue for INFO
 */
    fb_color_t warn_color;      /*
 * Orange for WARNING
 */
    fb_color_t error_color;     /*
 * Red for ERROR
 */
    fb_color_t debug_color;     /*
 * Gray for DEBUG
 */
} terminal_t;

/*
 * Global terminal
 */
extern terminal_t g_terminal;

/*
 * =============================================================================== Initialization and control ========================================================================================
 */

/*
 * Initializing the terminal
 */
void terminal_init(void);

/*
 * Setting the mode
 */
void terminal_set_mode(term_mode_t mode);

/*
 * Enable/disable prompt
 */
void terminal_enable_prompt(bool enable);
void terminal_prompt_toggle(void);

/*
 * Clearing the screen (while maintaining the buffer)
 */
void terminal_clear_screen(void);

/*
 * Clearing the entire buffer
 */
void terminal_clear_buffer(void);

/*
 * ============================================================================= Scrolling =================================================================================== Scrolling
 */

/*
 * Scroll up/down
 */
void terminal_scroll_up(i32 lines);
void terminal_scroll_down(i32 lines);
void terminal_scroll_to_top(void);
void terminal_scroll_to_bottom(void);

/*
 * Enable/disable auto-scroll
 */
void terminal_set_auto_scroll(bool enable);

/*
 * =============================================================================== Output to terminal =====================================================================================
 */

/*
 * Line output
 */
void terminal_print(const char* str);
void terminal_print_color(const char* str, fb_color_t fg, fb_color_t bg);

/*
 * Formatted string output
 */
void terminal_printf(const char* format, ...);

/*
 * Symbol output
 */
void terminal_putchar(char c);

/*
 * New line
 */
void terminal_newline(void);

/*
 * Information message (blue color)
 */
void terminal_info_printf(const char* format, ...);

/*
 * Warning (orange)
 */
void terminal_warn_printf(const char* format, ...);

/*
 * Error (red)
 */
void terminal_error_printf(const char* format, ...);

/*
 * Debug message (gray)
 */
void terminal_debug_printf(const char* format, ...);

/*
 * Successful message (green color)
 */
void terminal_success_printf(const char* format, ...);

/*
 * =============================================================================== Keyboard input ================================================================================
 */

/*
 * Input handler (called from the keyboard handler)
 */
void terminal_handle_input(char c);

/*
 * Get input string (blocking)
 */
char* terminal_getline(void);

/*
 * Check if there is input
 */
bool terminal_input_available(void);

/*
 * ============================================================================== Status and information =====================================================================================
 */

/*
 * Get current scroll position
 */
i32 terminal_get_scroll_position(void);

/*
 * Redraw screen
 */
void terminal_refresh(void);

/*
 * Show/hide scrollbar
 */
void terminal_show_scrollbar(bool show);

void terminal_update_cursor(void);

void terminal_update_prompt_line(void);

//Input with custom prompt
char* terminal_input(const char *prompt);

void terminal_printf_nn(const char* format, ...);  // no newline
void terminal_print_nn(const char* str);           // no newline
void terminal_generate_prompt(char *buffer, int buf_size);

#endif
