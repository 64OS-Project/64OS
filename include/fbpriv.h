#ifndef FBPRIV_H
#define FBPRIV_H

#include <fb.h>

// Global framebuffer structure
extern framebuffer_t g_fb;

// Private functions
bool clip_coord(i32 *x, i32 *y);
void put_pixel_raw(i32 x, i32 y, u32 pixel);
void swap(i32 *a, i32 *b);
extern void fb_flush(void);

// Absolute value macros
#define ABS(x) ((x) < 0 ? -(x) : (x))

#endif
