#include <kernel/timer.h>
#include <asm/cpu.h>
#include <asm/io.h>

static u64 tsc_per_ms = 0;
static u64 tsc_khz = 0;

static void tsc_calibrate_with_pit(void) {
    /*
 * PIT channel 2, mode 0
 */
    outb(0x43, 0xB0);
    outb(0x42, 0xFF);
    outb(0x42, 0xFF);
    
    u64 start = rdtsc();
    for (int i = 0; i < 10000; i++) {
        inb(0x61);
    }
    u64 end = rdtsc();
    
    tsc_per_ms = (end - start) / 10;  /*
 * ~10ms
 */
    tsc_khz = tsc_per_ms;
}


static void tsc_calibrate_fixed(void) {
    tsc_per_ms = 2000000;
    tsc_khz = 2000000;
}

void timer_udelay(u32 us) {
    if (!tsc_per_ms) {
        tsc_calibrate_with_pit();
    }

    if (!tsc_per_ms) {
        tsc_calibrate_fixed();
    }
    
    u64 start = rdtsc();
    u64 need = (tsc_per_ms * us) / 1000;
    while (rdtsc() - start < need) {
        asm volatile("pause");
    }
}

void timer_mdelay(u32 ms) {
    timer_udelay(ms * 1000);
}

void timer_sdelay(u32 s) {
    timer_mdelay(s * 1000);
}

void timer_sleep(u32 ms) {
    if (!tsc_per_ms) timer_udelay(1);
    
    u64 start = timer_apic_ticks();
    u64 need = (u64)ms * timer_apic_freq() / 1000;
    
    while (timer_apic_ticks() - start < need) {
        asm volatile("hlt");
    }
}
