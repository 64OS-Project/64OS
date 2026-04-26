#include <fb.h>
#include <fbfont.h>
#include <libk/string.h>
#include <fbpriv.h>

//=============================================================================
// Font data tables
//=============================================================================

static const u8 (*font_uppercase[26])[12][1] = {
    &glyph_A, &glyph_B, &glyph_C, &glyph_D, &glyph_E,
    &glyph_F, &glyph_G, &glyph_H, &glyph_I, &glyph_J,
    &glyph_K, &glyph_L, &glyph_M, &glyph_N, &glyph_O,
    &glyph_P, &glyph_Q, &glyph_R, &glyph_S, &glyph_T,
    &glyph_U, &glyph_V, &glyph_W, &glyph_X, &glyph_Y,
    &glyph_Z
};

static const u8 (*font_lowercase[26])[12][1] = {
    &glyph_a, &glyph_b, &glyph_c, &glyph_d, &glyph_e,
    &glyph_f, &glyph_g, &glyph_h, &glyph_i, &glyph_j,
    &glyph_k, &glyph_l, &glyph_m, &glyph_n, &glyph_o,
    &glyph_p, &glyph_q, &glyph_r, &glyph_s, &glyph_t,
    &glyph_u, &glyph_v, &glyph_w, &glyph_x, &glyph_y,
    &glyph_z
};

static const u8 (*font_numbers[10])[12][1] = {
    &glyph_0, &glyph_1, &glyph_2, &glyph_3, &glyph_4,
    &glyph_5, &glyph_6, &glyph_7, &glyph_8, &glyph_9
};

static const u8 (*get_symbol_glyph(char c))[12][1] {
    switch(c) {
        case ' ': return &glyph_space;
        case '!': return &glyph_exclamation_mark;
        case '?': return &glyph_question_mark;
        case '$': return &glyph_dollar;
        case ':': return &glyph_colon;
        case '.': return &glyph_dot;
        case '_': return &glyph_underscore;
        case '~': return &glyph_tilde;
        case '@': return &glyph_at;
        case '#': return &glyph_hash;
        case '%': return &glyph_percent;
        case '^': return &glyph_caret;
        case '*': return &glyph_asterisk;
        case '(': return &glyph_left_parenthesis;
        case ')': return &glyph_right_parenthesis;
        case '-': return &glyph_minus;
        case '=': return &glyph_equals;
        case '+': return &glyph_plus;
        case '[': return &glyph_left_square_bracket;
        case ']': return &glyph_right_square_bracket;
        case '{': return &glyph_left_curly_brace;
        case '}': return &glyph_right_curly_brace;
        case ';': return &glyph_semicolon;
        case ',': return &glyph_comma;
        case '\'': return &glyph_single_quote;
        case '"': return &glyph_double_quote;
        case '<': return &glyph_left_angle_bracket;
        case '>': return &glyph_right_angle_bracket;
        case '/': return &glyph_slash;
        case '\\': return &glyph_backslash;
        case '|': return &glyph_pipe;
        case '`': return &glyph_backtick;
        case '&': return &glyph_ampersand;
        default: return &glyph_space;
    }
}

const u8 (*font_get_glyph(char c))[12][1] {
    if (c >= 'A' && c <= 'Z') {
        return font_uppercase[c - 'A'];
    } else if (c >= 'a' && c <= 'z') {
        return font_lowercase[c - 'a'];
    } else if (c >= '0' && c <= '9') {
        return font_numbers[c - '0'];
    } else {
        return get_symbol_glyph(c);
    }
}

//=============================================================================
// Text rendering
//=============================================================================

void fb_draw_char(char c, i32 x, i32 y, fb_color_t fg, fb_color_t bg) {
    const u8 (*glyph)[12][1] = font_get_glyph(c);
    if (!glyph) return;
    
    u32 fg_pixel = color_to_pixel(fg);
    u32 bg_pixel = color_to_pixel(bg);
    
    // Межбуквенный интервал (2 пикселя)
    const int LETTER_SPACING = 2;
    
    for (u8 row = 0; row < FONT_HEIGHT; row++) {
        u8 bits = (*glyph)[row][0];
        i32 py = y + row;
        
        for (u8 col = 0; col < FONT_WIDTH; col++) {
            i32 px = x + col;
            
            // Если это не последний столбец, идёт как обычно
            if (bits & (0x80 >> col)) {
                put_pixel_raw(px, py, fg_pixel);
            } else {
                put_pixel_raw(px, py, bg_pixel);
            }
        }
    }
}

void fb_draw_string(const char *str, i32 x, i32 y, fb_color_t fg, fb_color_t bg) {
    if (!str) return;
    
    i32 cur_x = x;
    i32 cur_y = y;
    const int LETTER_SPACING = 2;  // 2 пикселя между буквами
    
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += FONT_HEIGHT;
        } else {
            fb_draw_char(*str, cur_x, cur_y, fg, bg);
            cur_x += FONT_WIDTH + LETTER_SPACING;  // ← ДОБАВЛЯЕМ ОТСТУП
        }
        str++;
    }
}

void fb_draw_char_large(char c, i32 x, i32 y, u8 scale, 
                        fb_color_t fg, fb_color_t bg) {
    if (scale == 0) return;
    if (scale == 1) {
        fb_draw_char(c, x, y, fg, bg);
        return;
    }
    
    const u8 (*glyph)[12][1] = font_get_glyph(c);
    if (!glyph) return;
    
    u32 fg_pixel = color_to_pixel(fg);
    u32 bg_pixel = color_to_pixel(bg);
    
    for (u8 row = 0; row < FONT_HEIGHT; row++) {
        u8 bits = (*glyph)[row][0];
        
        for (u8 col = 0; col < FONT_WIDTH; col++) {
            u32 color = (bits & (0x80 >> col)) ? fg_pixel : bg_pixel;
            
            for (u8 sy = 0; sy < scale; sy++) {
                i32 py = y + row * scale + sy;
                for (u8 sx = 0; sx < scale; sx++) {
                    i32 px = x + col * scale + sx;
                    if (px >= 0 && px < (i32)g_fb.width && 
                        py >= 0 && py < (i32)g_fb.height) {
                        put_pixel_raw(px, py, color);
                    }
                }
            }
        }
    }
}

void fb_draw_string_large(const char *str, i32 x, i32 y, u8 scale,
                          fb_color_t fg, fb_color_t bg) {
    if (!str) return;
    
    i32 cur_x = x;
    i32 cur_y = y;
    
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += FONT_HEIGHT * scale;
        } else {
            fb_draw_char_large(*str, cur_x, cur_y, scale, fg, bg);
            cur_x += FONT_WIDTH * scale;
        }
        str++;
    }
}

u32 fb_text_width(const char *str) {
    if (!str) return 0;
    return strlen(str) * FONT_WIDTH;
}

u32 fb_text_width_large(const char *str, u8 scale) {
    if (!str) return 0;
    return strlen(str) * FONT_WIDTH * scale;
}

u32 fb_char_width(char c) {
    (void)c;
    return FONT_WIDTH;
}

u32 fb_char_width_large(char c, u8 scale) {
    (void)c;
    return FONT_WIDTH * scale;
}

void fb_draw_char_scaled(char c, i32 x, i32 y, i32 scale, fb_color_t fg, fb_color_t bg) {
    const u8 (*glyph)[12][1] = font_get_glyph(c);
    if (!glyph) return;
    
    u32 fg_pixel = color_to_pixel(fg);
    u32 bg_pixel = color_to_pixel(bg);
    
    if (scale > 0) {
        /*
 * Increase
 */
        for (u8 row = 0; row < FONT_HEIGHT; row++) {
            u8 bits = (*glyph)[row][0];
            
            for (u8 col = 0; col < FONT_WIDTH; col++) {
                u32 color = (bits & (0x80 >> col)) ? fg_pixel : bg_pixel;
                
                for (i32 sy = 0; sy < scale; sy++) {
                    i32 py = y + row * scale + sy;
                    for (i32 sx = 0; sx < scale; sx++) {
                        i32 px = x + col * scale + sx;
                        if (px >= 0 && px < (i32)g_fb.width && 
                            py >= 0 && py < (i32)g_fb.height) {
                            put_pixel_raw(px, py, color);
                        }
                    }
                }
            }
        }
    } else if (scale < 0) {
        /*
 * Decrease
 */
        i32 shrink = -scale;  /*
 * positive number to decrease
 */
        
        /*
 * For each pixel of the reduced image
 */
        for (u8 row = 0; row < FONT_HEIGHT; row += shrink) {
            i32 py = y + (row / shrink);
            if (py < 0 || py >= (i32)g_fb.height) continue;
            
            for (u8 col = 0; col < FONT_WIDTH; col += shrink) {
                i32 px = x + (col / shrink);
                if (px < 0 || px >= (i32)g_fb.width) continue;
                
                /*
 * Averaging a shrink x shrink pixel block
 */
                u32 total = 0;
                u32 count = 0;
                
                for (u8 sy = 0; sy < (u8)shrink && row + sy < FONT_HEIGHT; sy++) {
                    u8 bits = (*glyph)[row + sy][0];
                    for (u8 sx = 0; sx < (u8)shrink && col + sx < FONT_WIDTH; sx++) {
                        if (bits & (0x80 >> (col + sx))) {
                            total++;
                        }
                        count++;
                    }
                }
                
                /*
 * If more than half of the pixels in the block are filled
 */
                u32 color = (total > count/2) ? fg_pixel : bg_pixel;
                put_pixel_raw(px, py, color);
            }
        }
    } else {
        /*
 * scale == 0 - don't draw anything
 */
        return;
    }
}

/*
 * Drawing a line with scale
 */
void fb_draw_string_scaled(const char *str, i32 x, i32 y, i32 scale, fb_color_t fg, fb_color_t bg) {
    if (!str) return;
    
    i32 cur_x = x;
    i32 cur_y = y;
    i32 char_width = fb_char_width_scaled(' ', scale);
    i32 char_height = (scale > 0) ? FONT_HEIGHT * scale : FONT_HEIGHT / (-scale);
    
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += char_height + 2;  /*
 * +2 for line spacing
 */
        } else {
            fb_draw_char_scaled(*str, cur_x, cur_y, scale, fg, bg);
            cur_x += char_width;
        }
        str++;
    }
}

/*
 * Symbol width taking into account scale
 */
u32 fb_char_width_scaled(char c, i32 scale) {
    (void)c;
    if (scale > 0) {
        return FONT_WIDTH * scale;
    } else if (scale < 0) {
        return FONT_WIDTH / (-scale);
    } else {
        return 0;
    }
}

/*
 * Line width taking into account scale
 */
u32 fb_text_width_scaled(const char *str, i32 scale) {
    if (!str) return 0;
    return strlen(str) * fb_char_width_scaled(' ', scale);
}
