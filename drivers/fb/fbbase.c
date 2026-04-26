#include <fb.h>
#include <fbpriv.h>
#include <kernel/driver.h>
#include <mbstruct.h>
#include <kernel/paging.h>
#include <libk/string.h>
#include <mm/heap.h>

framebuffer_t g_fb = {0};
extern multiboot2_info_t mbinfo;

static int fb_probe(driver_t *drv) {
    (void)drv;
    if (mbinfo.framebuffer.addr) { return 0; }
    return 1;
}

static int fb_init_driver(driver_t *drv) {
    (void)drv;
    if (g_fb.initialized) {
        return 0;
    }
    return -1;
}
    
driver_t g_fb_driver = {
    .name = "framebuffer",
    .desc = "Graphical Framebuffer Driver",
    .critical_level = DRIVER_CRITICAL_0, /*
 * The system will not boot without Framebuffer, Because GNU GRUB has already put the GPU into linear buffer mode, and VGA is unavailable Although it will load, it will be useless, Accordingly, Framebuffer is required.
 */
    .probe = fb_probe,
    .init = fb_init_driver,
    .remove = NULL  // Framebuffer cannot be removed
};

u32 color_to_pixel(fb_color_t c) {
    if (g_fb.format == FB_FORMAT_RGB888) {
        return (c.r << 16) | (c.g << 8) | c.b;
    } else if (g_fb.format == FB_FORMAT_BGR888) {
        return (c.b << 16) | (c.g << 8) | c.r;
    } else if (g_fb.format == FB_FORMAT_RGB565) {
        return ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
    }
    return 0;
}

bool clip_coord(i32 *x, i32 *y) {
    if (g_fb.clip_enabled) {
        if (*x < g_fb.clip_x1 || *x >= g_fb.clip_x2 || 
            *y < g_fb.clip_y1 || *y >= g_fb.clip_y2) {
            return false;
        }
    } else {
        if (*x < 0 || *x >= (i32)g_fb.width || 
            *y < 0 || *y >= (i32)g_fb.height) {
            return false;
        }
    }
    return true;
}

void put_pixel_raw(i32 x, i32 y, u32 pixel) {
    if (x < 0 || x >= (i32)g_fb.width || y < 0 || y >= (i32)g_fb.height) {
        return;
    }
    
    // Рисуем в back buffer, если он есть
    u8 *ptr;
    if (g_fb.back_buffer) {
        ptr = (u8*)g_fb.back_buffer + y * g_fb.pitch + x * g_fb.bytes_per_pixel;
    } else {
        ptr = (u8*)g_fb.virt_addr + y * g_fb.pitch + x * g_fb.bytes_per_pixel;
    }
    
    if (g_fb.bytes_per_pixel == 4) {
        *(u32*)ptr = pixel;
    } else if (g_fb.bytes_per_pixel == 3) {
        ptr[0] = pixel & 0xFF;
        ptr[1] = (pixel >> 8) & 0xFF;
        ptr[2] = (pixel >> 16) & 0xFF;
    } else if (g_fb.bytes_per_pixel == 2) {
        *(u16*)ptr = (u16)pixel;
    }
}

void swap(i32 *a, i32 *b) {
    i32 t = *a;
    *a = *b;
    *b = t;
}

bool framebuffer_init(u64 phys_addr, u32 width, u32 height, 
                      u32 pitch, u8 bpp) {
    if (!phys_addr || width == 0 || height == 0) return false;
 
    g_fb.phys_addr = phys_addr;
    g_fb.virt_addr = (void*)phys_addr;
    g_fb.width = width;
    g_fb.height = height;
    g_fb.pitch = pitch;
    g_fb.bpp = bpp;
    g_fb.bytes_per_pixel = (bpp + 7) / 8;

    g_fb.back_buffer = malloc(g_fb.height * g_fb.pitch);
    if (g_fb.back_buffer) {
    	memset(g_fb.back_buffer, 0, g_fb.height * g_fb.pitch);
    }
    
    // Detect format
    if (bpp == 32) g_fb.format = FB_FORMAT_RGB888;
    else if (bpp == 16) g_fb.format = FB_FORMAT_RGB565;
    else g_fb.format = FB_FORMAT_UNKNOWN;
    
    g_fb.clip_enabled = false;
    g_fb.initialized = true;
    
    fb_clear(FB_BLACK);
    return true;
}

framebuffer_t *framebuffer_get(void) {
    return g_fb.initialized ? &g_fb : NULL;
}

void fb_put_pixel(i32 x, i32 y, fb_color_t color) {
    if (!clip_coord(&x, &y)) return;
    put_pixel_raw(x, y, color_to_pixel(color));
}

fb_color_t fb_get_pixel(i32 x, i32 y) {
    fb_color_t black = FB_BLACK;
    if (!clip_coord(&x, &y)) return black;
    
    u8 *ptr = (u8*)g_fb.virt_addr + y * g_fb.pitch + x * g_fb.bytes_per_pixel;
    u32 pixel = 0;
    
    if (g_fb.bytes_per_pixel == 4) pixel = *(u32*)ptr;
    else if (g_fb.bytes_per_pixel == 3) pixel = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16);
    else if (g_fb.bytes_per_pixel == 2) pixel = *(u16*)ptr;
    
    // Convert back (simplified)
    if (g_fb.format == FB_FORMAT_RGB888) {
        return (fb_color_t){ (pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF, 255 };
    }
    return black;
}

void fb_clear(fb_color_t color) {
    if (g_fb.back_buffer) {
        // Очищаем back buffer
        u32 pixel = color_to_pixel(color);
        u32 total_pixels = g_fb.width * g_fb.height;
        u32 *ptr = (u32*)g_fb.back_buffer;
        
        for (u32 i = 0; i < total_pixels; i++) {
            ptr[i] = pixel;
        }
    } else {
        // Старый код для прямого рисования
        fb_fill_rect(0, 0, g_fb.width, g_fb.height, color);
    }
}

fb_color_t fb_rgb(u8 r, u8 g, u8 b) {
    return (fb_color_t){r, g, b, 255};
}

fb_color_t fb_hex(u32 hex) {
    return (fb_color_t){ (hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, 255 };
}

u32 fb_get_width(void) {
    return g_fb.width;
}

u32 fb_get_height(void) {
    return g_fb.height;
}

bool fb_is_initialized(void) {
    return g_fb.initialized;
}

u8 fb_get_bpp(void) {
    return g_fb.bpp;
}

void fb_swap_buffers(void) {
    if (!g_fb.back_buffer) return;
    
    // Копируем бэк-буфер на экран
    memcpy(g_fb.virt_addr, g_fb.back_buffer, g_fb.height * g_fb.pitch);
}

void fb_clear_back(void) {
    if (!g_fb.back_buffer) return;
    memset(g_fb.back_buffer, 0, g_fb.height * g_fb.pitch);
}