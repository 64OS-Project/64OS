#include <kernel/panic.h>
#include <kernel/types.h>
#include <libk/string.h>
#include <asm/cpu.h>
#include <fb.h>

static bool panic_running = false;

void panic(const char *stop) {
    if (panic_running) {
        if (check_interrupt_status()) {
            intd();
        }
        halt();
    }

    if (check_interrupt_status()) {
        intd();
    }
    panic_running = true;

    i32 x = 10;   //Left indent
    i32 y = 10;   //Top margin

    fb_clear(FB_RED);
    
    //Panic headline
    fb_draw_string("KERNEL PANIC - NOT SYNCING", x, y, FB_RED, FB_BLACK);
    fb_draw_string("* STOP CODE: ", x, y + 30, FB_RED, FB_BLACK);
    
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s", stop);
    fb_draw_string(buffer, x + (FONT_WIDTH * strlen("* STOP CODE: ")), y + 30, FB_WHITE, FB_BLACK);
    memset(buffer, 0, sizeof(buffer));

    for (;;) {
	    halt();
    }
}
