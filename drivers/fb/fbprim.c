#include <fb.h>
#include <fbpriv.h>

//=============================================================================
// Rectangle primitives
//=============================================================================

void fb_draw_rect(i32 x, i32 y, u32 w, u32 h, fb_color_t color) {
    fb_draw_line(x, y, x + w - 1, y, color);
    fb_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
    fb_draw_line(x + w - 1, y + h - 1, x, y + h - 1, color);
    fb_draw_line(x, y + h - 1, x, y, color);
}

void fb_fill_rect(i32 x, i32 y, u32 w, u32 h, fb_color_t color) {
    for (u32 i = 0; i < h; i++) {
        fb_draw_line(x, y + i, x + w - 1, y + i, color);
    }
}

//=============================================================================
// Rounded rectangle primitives
//=============================================================================

void fb_draw_rounded_rect(i32 x, i32 y, u32 w, u32 h, 
                          u32 radius, fb_color_t color) {
    if (radius == 0) {
        fb_draw_rect(x, y, w, h, color);
        return;
    }
    
    if (radius > w/2) radius = w/2;
    if (radius > h/2) radius = h/2;
    
    // Top and bottom lines
    fb_draw_line(x + radius, y, x + w - radius - 1, y, color);
    fb_draw_line(x + radius, y + h - 1, x + w - radius - 1, y + h - 1, color);
    
    // Left and right lines
    fb_draw_line(x, y + radius, x, y + h - radius - 1, color);
    fb_draw_line(x + w - 1, y + radius, x + w - 1, y + h - radius - 1, color);
    
    // Corners using Bresenham's circle algorithm
    i32 cx, cy;
    i32 dx = radius;
    i32 dy = 0;
    i32 err = 0;
    
    while (dx >= dy) {
        // Top-left
        cx = x + radius - dx;
        cy = y + radius - dy;
        fb_put_pixel(cx, cy, color);
        
        cx = x + radius - dy;
        cy = y + radius - dx;
        fb_put_pixel(cx, cy, color);
        
        // Top-right
        cx = x + w - radius - 1 + dx;
        cy = y + radius - dy;
        fb_put_pixel(cx, cy, color);
        
        cx = x + w - radius - 1 + dy;
        cy = y + radius - dx;
        fb_put_pixel(cx, cy, color);
        
        // Bottom-left
        cx = x + radius - dx;
        cy = y + h - radius - 1 + dy;
        fb_put_pixel(cx, cy, color);
        
        cx = x + radius - dy;
        cy = y + h - radius - 1 + dx;
        fb_put_pixel(cx, cy, color);
        
        // Bottom-right
        cx = x + w - radius - 1 + dx;
        cy = y + h - radius - 1 + dy;
        fb_put_pixel(cx, cy, color);
        
        cx = x + w - radius - 1 + dy;
        cy = y + h - radius - 1 + dx;
        fb_put_pixel(cx, cy, color);
        
        dy++;
        err += 2 * dy + 1;
        if (err > 2 * dx) {
            dx--;
            err -= 2 * dx + 2;
        }
    }
}

void fb_fill_rounded_rect(i32 x, i32 y, u32 w, u32 h, 
                          u32 radius, fb_color_t color) {
    if (radius == 0) {
        fb_fill_rect(x, y, w, h, color);
        return;
    }
    
    if (radius > w/2) radius = w/2;
    if (radius > h/2) radius = h/2;
    
    u32 pixel = color_to_pixel(color);
    
    // Fill center rectangle
    fb_fill_rect(x + radius, y, w - 2 * radius, radius, color);
    fb_fill_rect(x + radius, y + h - radius, w - 2 * radius, radius, color);
    fb_fill_rect(x, y + radius, w, h - 2 * radius, color);
    
    // Fill corners
    for (u32 dy = 0; dy < radius; dy++) {
        u32 dx = radius - 1;
        while (dx * dx + dy * dy > radius * radius) {
            dx--;
        }
        
        // Top-left
        for (u32 ix = 0; ix < dx; ix++) {
            put_pixel_raw(x + radius - 1 - ix, y + radius - 1 - dy, pixel);
            put_pixel_raw(x + radius - 1 - dy, y + radius - 1 - ix, pixel);
        }
        
        // Top-right
        for (u32 ix = 0; ix < dx; ix++) {
            put_pixel_raw(x + w - radius + ix, y + radius - 1 - dy, pixel);
            put_pixel_raw(x + w - radius + dy, y + radius - 1 - ix, pixel);
        }
        
        // Bottom-left
        for (u32 ix = 0; ix < dx; ix++) {
            put_pixel_raw(x + radius - 1 - ix, y + h - radius + dy, pixel);
            put_pixel_raw(x + radius - 1 - dy, y + h - radius + ix, pixel);
        }
        
        // Bottom-right
        for (u32 ix = 0; ix < dx; ix++) {
            put_pixel_raw(x + w - radius + ix, y + h - radius + dy, pixel);
            put_pixel_raw(x + w - radius + dy, y + h - radius + ix, pixel);
        }
    }
}

//=============================================================================
// Circle primitives
//=============================================================================

void fb_draw_circle(i32 cx, i32 cy, u32 r, fb_color_t color) {
    i32 f = 1 - r;
    i32 ddF_x = 0;
    i32 ddF_y = -2 * r;
    i32 x = 0;
    i32 y = r;
    
    fb_put_pixel(cx, cy + r, color);
    fb_put_pixel(cx, cy - r, color);
    fb_put_pixel(cx + r, cy, color);
    fb_put_pixel(cx - r, cy, color);
    
    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x + 1;
        
        fb_put_pixel(cx + x, cy + y, color);
        fb_put_pixel(cx - x, cy + y, color);
        fb_put_pixel(cx + x, cy - y, color);
        fb_put_pixel(cx - x, cy - y, color);
        fb_put_pixel(cx + y, cy + x, color);
        fb_put_pixel(cx - y, cy + x, color);
        fb_put_pixel(cx + y, cy - x, color);
        fb_put_pixel(cx - y, cy - x, color);
    }
}

void fb_fill_circle(i32 cx, i32 cy, u32 r, fb_color_t color) {
    for (u32 i = 0; i <= r; i++) {
        fb_draw_circle(cx, cy, i, color);
    }
}

//=============================================================================
// Ellipse primitives
//=============================================================================

void fb_draw_ellipse(i32 cx, i32 cy, u32 rx, u32 ry, fb_color_t color) {
    if (rx == ry) {
        fb_draw_circle(cx, cy, rx, color);
        return;
    }
    
    i32 x = 0;
    i32 y = ry;
    
    i32 rx2 = rx * rx;
    i32 ry2 = ry * ry;
    i32 two_rx2 = 2 * rx2;
    i32 two_ry2 = 2 * ry2;
    
    i32 p = ry2 - rx2 * ry + (rx2 / 4);
    
    // Region 1
    while (two_ry2 * x < two_rx2 * y) {
        fb_put_pixel(cx + x, cy + y, color);
        fb_put_pixel(cx - x, cy + y, color);
        fb_put_pixel(cx + x, cy - y, color);
        fb_put_pixel(cx - x, cy - y, color);
        
        if (p < 0) {
            x++;
            p += two_ry2 * x + ry2;
        } else {
            x++;
            y--;
            p += two_ry2 * x - two_rx2 * y + ry2;
        }
    }
    
    p = ry2 * (x + 1) * (x + 1) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    
    // Region 2
    while (y >= 0) {
        fb_put_pixel(cx + x, cy + y, color);
        fb_put_pixel(cx - x, cy + y, color);
        fb_put_pixel(cx + x, cy - y, color);
        fb_put_pixel(cx - x, cy - y, color);
        
        if (p > 0) {
            y--;
            p -= two_rx2 * y + rx2;
        } else {
            x++;
            y--;
            p += two_ry2 * x - two_rx2 * y + rx2;
        }
    }
}

void fb_fill_ellipse(i32 cx, i32 cy, u32 rx, u32 ry, fb_color_t color) {
    if (rx == 0 || ry == 0) return;
    
    if (rx == ry) {
        fb_fill_circle(cx, cy, rx, color);
        return;
    }
    
    u32 pixel = color_to_pixel(color);
    i32 x, y;
    
    for (y = 0; y <= (i32)ry; y++) {
        i32 line_width = 0;
        i32 y2 = y * y;
        
        while (line_width * line_width * ry * ry + y2 * rx * rx <= rx * rx * ry * ry) {
            line_width++;
        }
        line_width--;
        
        for (x = -line_width; x <= line_width; x++) {
            i32 px = cx + x;
            i32 py1 = cy - y;
            i32 py2 = cy + y;
            
            if (px >= 0 && px < (i32)g_fb.width) {
                if (py1 >= 0 && py1 < (i32)g_fb.height) {
                    put_pixel_raw(px, py1, pixel);
                }
                if (py2 >= 0 && py2 < (i32)g_fb.height && y != 0) {
                    put_pixel_raw(px, py2, pixel);
                }
            }
        }
    }
}

//=============================================================================
// Line primitives
//=============================================================================

void fb_draw_line(i32 x1, i32 y1, i32 x2, i32 y2, fb_color_t color) {
    i32 dx = ABS(x2 - x1);
    i32 dy = ABS(y2 - y1);
    i32 sx = (x1 < x2) ? 1 : -1;
    i32 sy = (y1 < y2) ? 1 : -1;
    i32 err = dx - dy;
    
    while (1) {
        fb_put_pixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        
        i32 e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void fb_draw_line_thick(i32 x1, i32 y1, i32 x2, i32 y2, 
                        u32 thickness, fb_color_t color) {
    if (thickness <= 1) {
        fb_draw_line(x1, y1, x2, y2, color);
        return;
    }
    
    i32 dx = x2 - x1;
    i32 dy = y2 - y1;
    
    if (ABS(dx) > ABS(dy)) {
        // Horizontal-ish
        i32 offset = thickness / 2;
        for (u32 i = 0; i < thickness; i++) {
            fb_draw_line(x1, y1 - offset + i, x2, y2 - offset + i, color);
        }
    } else {
        // Vertical-ish
        i32 offset = thickness / 2;
        for (u32 i = 0; i < thickness; i++) {
            fb_draw_line(x1 - offset + i, y1, x2 - offset + i, y2, color);
        }
    }
}

//=============================================================================
// Triangle primitives
//=============================================================================

void fb_draw_triangle(i32 x1, i32 y1, i32 x2, i32 y2, 
                      i32 x3, i32 y3, fb_color_t color) {
    fb_draw_line(x1, y1, x2, y2, color);
    fb_draw_line(x2, y2, x3, y3, color);
    fb_draw_line(x3, y3, x1, y1, color);
}

void fb_fill_triangle(i32 x1, i32 y1, i32 x2, i32 y2, 
                      i32 x3, i32 y3, fb_color_t color) {
    u32 pixel = color_to_pixel(color);
    
    // Sort the vertices by y
    if (y1 > y2) { swap(&x1, &x2); swap(&y1, &y2); }
    if (y1 > y3) { swap(&x1, &x3); swap(&y1, &y3); }
    if (y2 > y3) { swap(&x2, &x3); swap(&y2, &y3); }
    
    // Helper function for drawing a horizontal line
    void draw_horizontal_line(i32 y, i32 x_start, i32 x_end) {
        if (x_start > x_end) swap(&x_start, &x_end);
        for (i32 x = x_start; x <= x_end; x++) {
            i32 px = x;
            i32 py = y;
            if (clip_coord(&px, &py)) {
                put_pixel_raw(px, py, pixel);
            }
        }
    }
    
    // Handling Special Cases
    if (y1 == y2 && y2 == y3) {
        return;
    }
    
    if (y1 == y2) {
        // Flat top
        i32 x_left = x1;
        i32 x_right = x2;
        if (x_left > x_right) swap(&x_left, &x_right);
        
        for (i32 y = y1; y <= y3; y++) {
            i32 x_start = x_left + (x3 - x_left) * (y - y1) / (y3 - y1);
            i32 x_end = x_right + (x3 - x_right) * (y - y2) / (y3 - y2);
            draw_horizontal_line(y, x_start, x_end);
        }
    }
    else if (y2 == y3) {
        // Flat bottom
        i32 x_left = x2;
        i32 x_right = x3;
        if (x_left > x_right) swap(&x_left, &x_right);
        
        for (i32 y = y1; y <= y3; y++) {
            i32 x_start = x1 + (x_left - x1) * (y - y1) / (y2 - y1);
            i32 x_end = x1 + (x_right - x1) * (y - y1) / (y3 - y1);
            draw_horizontal_line(y, x_start, x_end);
        }
    }
    else {
        // General case - split into two triangles
        i32 x4 = x1 + (i32)((i64)(x3 - x1) * (y2 - y1) / (y3 - y1));
        
        // Upper part (y1 to y2)
        for (i32 y = y1; y <= y2; y++) {
            i32 x_start = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
            i32 x_end = x1 + (x4 - x1) * (y - y1) / (y2 - y1);
            draw_horizontal_line(y, x_start, x_end);
        }
        
        // Bottom part (y2 to y3)
        for (i32 y = y2; y <= y3; y++) {
            i32 x_start = x2 + (x3 - x2) * (y - y2) / (y3 - y2);
            i32 x_end = x4 + (x3 - x4) * (y - y2) / (y3 - y2);
            draw_horizontal_line(y, x_start, x_end);
        }
    }
}

//=============================================================================
// Polygon primitives
//=============================================================================

void fb_draw_polygon(i32 *x_points, i32 *y_points, u32 num_points, 
                     fb_color_t color) {
    if (num_points < 3) return;
    
    for (u32 i = 0; i < num_points - 1; i++) {
        fb_draw_line(x_points[i], y_points[i], x_points[i+1], y_points[i+1], color);
    }
    fb_draw_line(x_points[num_points-1], y_points[num_points-1], x_points[0], y_points[0], color);
}

void fb_fill_polygon(i32 *x_points, i32 *y_points, u32 num_points, 
                     fb_color_t color) {
    if (num_points < 3) return;
    
    // Find min and max y
    i32 min_y = y_points[0];
    i32 max_y = y_points[0];
    for (u32 i = 1; i < num_points; i++) {
        if (y_points[i] < min_y) min_y = y_points[i];
        if (y_points[i] > max_y) max_y = y_points[i];
    }
    
    // Scanline algorithm
    for (i32 y = min_y; y <= max_y; y++) {
        i32 intersections[64];
        u32 int_count = 0;
        
        for (u32 i = 0; i < num_points; i++) {
            u32 j = (i + 1) % num_points;
            
            if ((y_points[i] <= y && y_points[j] > y) || 
                (y_points[j] <= y && y_points[i] > y)) {
                
                i32 x = x_points[i] + (y - y_points[i]) * 
                          (x_points[j] - x_points[i]) / (y_points[j] - y_points[i]);
                intersections[int_count++] = x;
            }
        }
        
        // Sort intersections
        for (u32 i = 0; i < int_count; i++) {
            for (u32 j = i + 1; j < int_count; j++) {
                if (intersections[i] > intersections[j]) {
                    swap(&intersections[i], &intersections[j]);
                }
            }
        }
        
        // Fill between pairs
        for (u32 i = 0; i + 1 < int_count; i += 2) {
            for (i32 x = intersections[i]; x <= intersections[i+1]; x++) {
                fb_put_pixel(x, y, color);
            }
        }
    }
}

//=============================================================================
// Clipping
//=============================================================================

void fb_set_clip(i32 x1, i32 y1, i32 x2, i32 y2) {
    g_fb.clip_x1 = (x1 < 0) ? 0 : x1;
    g_fb.clip_y1 = (y1 < 0) ? 0 : y1;
    g_fb.clip_x2 = (x2 >= (i32)g_fb.width) ? g_fb.width : x2;
    g_fb.clip_y2 = (y2 >= (i32)g_fb.height) ? g_fb.height : y2;
    g_fb.clip_enabled = true;
}

void fb_reset_clip(void) {
    g_fb.clip_x1 = 0;
    g_fb.clip_y1 = 0;
    g_fb.clip_x2 = g_fb.width;
    g_fb.clip_y2 = g_fb.height;
    g_fb.clip_enabled = false;
}

void fb_disable_clip(void) {
    g_fb.clip_enabled = false;
}

#define LOGO_WIDTH  64
#define LOGO_HEIGHT 64

void draw_os_logo(i32 x, i32 y, const unsigned char *bitmap) {
    const unsigned char *pixel = bitmap;
    
    for (u32 row = 0; row < LOGO_HEIGHT; row++) {
        for (u32 col = 0; col < LOGO_WIDTH; col++) {
            u8 r = *pixel++;
            u8 g = *pixel++;
            u8 b = *pixel++;
            
            // Skipping black pixels (making the background transparent)
            if (r == 0 && g == 0 && b == 0) {
                continue;
            }
            
            fb_put_pixel(x + col, y + row, fb_rgb(r, g, b));
        }
    }
}
