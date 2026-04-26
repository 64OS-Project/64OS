#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <kernel/types.h>

// Pixel format
typedef enum {
    FB_FORMAT_RGB888,
    FB_FORMAT_BGR888,
    FB_FORMAT_RGB565,
    FB_FORMAT_UNKNOWN
} fb_pixel_format_t;

// Color (32-bit RGBA)
typedef struct {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
} fb_color_t;

typedef enum {
    FB_MODE_TEXT,      //Text buffer (terminal)
    FB_MODE_GRAPHICS   //Graphics Buffer (GUI)
} fb_mode_t;

// Predefined colors
#define FB_BLACK        (fb_color_t){0, 0, 0, 255}
#define FB_WHITE        (fb_color_t){255, 255, 255, 255}
#define FB_RED          (fb_color_t){255, 0, 0, 255}
#define FB_GREEN        (fb_color_t){0, 255, 0, 255}
#define FB_BLUE         (fb_color_t){0, 0, 255, 255}
#define FB_YELLOW       (fb_color_t){255, 255, 0, 255}
#define FB_CYAN         (fb_color_t){0, 255, 255, 255}
#define FB_MAGENTA      (fb_color_t){255, 0, 255, 255}
#define FB_GRAY         (fb_color_t){128, 128, 128, 255}
#define FB_LIGHT_GRAY   (fb_color_t){192, 192, 192, 255}
#define FB_DARK_GRAY    (fb_color_t){64, 64, 64, 255}

#define FONT_HEIGHT 12
#define FONT_WIDTH 8

// Main framebuffer structure
typedef struct {
    u64 phys_addr;
    void *virt_addr;
    u32 width;
    u32 height;
    u32 pitch;
    u8 bpp;
    fb_pixel_format_t format;
    u32 bytes_per_pixel;
    fb_mode_t mode;
    //Text buffer (for terminal)
    u8 *text_buffer;         //terminal buffer
    u32 text_buffer_size;
    bool text_buffer_dirty;
    void *back_buffer;
    
    //Graphics buffer (for GUI)
    u8 *graphics_buffer;     //graphics buffer
    u32 graphics_buffer_size;
    bool graphics_buffer_dirty;
    
    //Current active buffer to display
    u8 *active_buffer;
    
    //Switch handlers
    void (*on_switch_to_text)(void);
    void (*on_switch_to_graphics)(void);
    
    // Clipping
    bool clip_enabled;
    i32 clip_x1, clip_y1;
    i32 clip_x2, clip_y2;
    
    bool initialized;
} framebuffer_t;

// Initialization
bool framebuffer_init(u64 phys_addr, u32 width, u32 height, 
                      u32 pitch, u8 bpp);
framebuffer_t *framebuffer_get(void);

// Core pixel operations
void fb_put_pixel(i32 x, i32 y, fb_color_t color);
fb_color_t fb_get_pixel(i32 x, i32 y);
void fb_clear(fb_color_t color);

// Color utilities
fb_color_t fb_rgb(u8 r, u8 g, u8 b);
fb_color_t fb_hex(u32 hex);
u32 color_to_pixel(fb_color_t c);

// Rectangle primitives
void fb_draw_rect(i32 x, i32 y, u32 w, u32 h, fb_color_t color);
void fb_fill_rect(i32 x, i32 y, u32 w, u32 h, fb_color_t color);

// Rounded rectangle primitives
void fb_draw_rounded_rect(i32 x, i32 y, u32 w, u32 h, 
                          u32 radius, fb_color_t color);
void fb_fill_rounded_rect(i32 x, i32 y, u32 w, u32 h, 
                          u32 radius, fb_color_t color);

// Circle primitives
void fb_draw_circle(i32 cx, i32 cy, u32 r, fb_color_t color);
void fb_fill_circle(i32 cx, i32 cy, u32 r, fb_color_t color);

// Ellipse primitives
void fb_draw_ellipse(i32 cx, i32 cy, u32 rx, u32 ry, fb_color_t color);
void fb_fill_ellipse(i32 cx, i32 cy, u32 rx, u32 ry, fb_color_t color);

// Triangle primitives
void fb_draw_triangle(i32 x1, i32 y1, i32 x2, i32 y2, 
                      i32 x3, i32 y3, fb_color_t color);
void fb_fill_triangle(i32 x1, i32 y1, i32 x2, i32 y2, 
                      i32 x3, i32 y3, fb_color_t color);

// Line primitives
void fb_draw_line(i32 x1, i32 y1, i32 x2, i32 y2, fb_color_t color);
void fb_draw_line_thick(i32 x1, i32 y1, i32 x2, i32 y2, 
                        u32 thickness, fb_color_t color);

// Polygon primitives
void fb_draw_polygon(i32 *x_points, i32 *y_points, u32 num_points, 
                     fb_color_t color);
void fb_fill_polygon(i32 *x_points, i32 *y_points, u32 num_points, 
                     fb_color_t color);

// Clipping
void fb_set_clip(i32 x1, i32 y1, i32 x2, i32 y2);
void fb_reset_clip(void);
void fb_disable_clip(void);

// Utility
u32 fb_get_width(void);
u32 fb_get_height(void);
bool fb_is_initialized(void);
u8 fb_get_bpp(void);

// Text rendering
void fb_draw_char(char c, i32 x, i32 y, fb_color_t fg, fb_color_t bg);
void fb_draw_string(const char *str, i32 x, i32 y, fb_color_t fg, fb_color_t bg);
void fb_draw_char_large(char c, i32 x, i32 y, u8 scale, fb_color_t fg, fb_color_t bg);
void fb_draw_string_large(const char *str, i32 x, i32 y, u8 scale, fb_color_t fg, fb_color_t bg);
u32 fb_text_width(const char *str);
u32 fb_text_width_large(const char *str, u8 scale);
u32 fb_char_width(char c);
void fb_draw_char_scaled(char c, i32 x, i32 y, i32 scale, fb_color_t fg, fb_color_t bg);
void fb_draw_string_scaled(const char *str, i32 x, i32 y, i32 scale, fb_color_t fg, fb_color_t bg);
u32 fb_text_width_scaled(const char *str, i32 scale);
u32 fb_char_width_scaled(char c, i32 scale);
u32 fb_char_width_large(char c, u8 scale);

void draw_os_logo(i32 x, i32 y, const unsigned char *bitmap);

void fb_swap_buffers(void);
void fb_clear_back(void);

#define FB_RGB(r, g, b) ((fb_color_t){(r), (g), (b), 255})
#define FB_RGBA(r, g, b, a) ((fb_color_t){(r), (g), (b), (a)})

#endif
