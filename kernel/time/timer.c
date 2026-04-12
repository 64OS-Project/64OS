#include <kernel/timer.h>
#include <apic.h>
#include <apicregs.h>
#include <asm/cpu.h>
#include <asm/io.h>
#include <ktime/clock.h>
#include <ioapic.h>
#include <kernel/terminal.h>
#include <kernel/sched.h>

extern terminal_t g_terminal;
volatile u32 seconds = 0;

#define TIMER_VECTOR        0x20
#define PIT_BASE_FREQ       1193180U
#define PIT_MAX_COUNT       0xFFFFU
#define APIC_DIVISOR        16
#define CALIBRATION_MS      50
#define CALIBRATION_LOOPS   5

struct {
    u64 ticks;
    u32 period; 
    u32 tsc_khz;
    u32 apic_bus_khz;
    u32 divider;
    u32 init_count;
    bool calibrated;
} g_timer = {0};


static inline void pit_set_mode(u8 channel, u8 mode, u8 access);
static inline void pit_write_counter(u16 count);
static inline u16 pit_read_counter(void);
static inline u16 pit_read_counter_verified(void);
static void pit_busy_wait(u32 ms);
static void io_busy_wait(u32 iterations);
static u32 timer_tsc_calibrate_precise(void);
static u32 timer_apic_calibrate_with_tsc(u32 expected_ms);
static bool timer_verify_calibration(void);


static void io_busy_wait(u32 iterations) {
    for (u32 i = 0; i < iterations; i++) {
        outb(0x80, 0);  /*
 * Write to port 0x80 (POST card)
 */
    }
}

static inline void pit_set_mode(u8 channel, u8 mode, u8 access) {
    u8 cmd = (channel << 6) | (access << 4) | (mode << 1);
    outb(0x43, cmd);
    io_busy_wait(10);
}

static inline void pit_write_counter(u16 count) {
    outb(0x40, count & 0xFF);
    io_busy_wait(10);
    outb(0x40, (count >> 8) & 0xFF);
    io_busy_wait(10);
}

static inline u16 pit_read_counter(void) {
    outb(0x43, 0x80);
    io_busy_wait(10);
    u8 low = inb(0x40);
    io_busy_wait(10);
    u8 high = inb(0x40);
    io_busy_wait(10);
    return (u16)(low | (high << 8));
}

static inline u16 pit_read_counter_verified(void) {
    u16 val1, val2;
    int attempts = 0;
    
    do {
        val1 = pit_read_counter();
        val2 = pit_read_counter();
        attempts++;

        if (attempts > 10) {
            return val1;
        }

    } while (val1 > val2 + 10 || val2 > val1 + 10);
    
    return (val1 + val2) / 2;
}

static void pit_busy_wait(u32 ms) {
    pit_set_mode(0, 0, 3);  /*
 * Mode 0, lobyte/hibyte
 */
    
    u32 pit_ticks = (PIT_BASE_FREQ * ms) / 1000;
    if (pit_ticks > PIT_MAX_COUNT) pit_ticks = PIT_MAX_COUNT;
    if (pit_ticks < 100) pit_ticks = 100;
    
    pit_write_counter((u16)pit_ticks);

    u16 curr;
    do {
        curr = pit_read_counter();
        io_busy_wait(10);
    } while (curr > 1);
}


static u32 timer_tsc_calibrate_precise(void) {
    u64 tsc_start, tsc_end;
    u64 total_tsc = 0;
    u32 valid_measurements = 0;
    
    for (int sample = 0; sample < CALIBRATION_LOOPS; sample++) {
        u16 pit_initial = PIT_MAX_COUNT;
        
        /*
 * Mode 0: interrupt on terminal count
 */
        pit_set_mode(0, 0, 3);
        pit_write_counter(pit_initial);
        
        io_busy_wait(100);

        tsc_start = rdtsc();
        
        u16 prev_count = pit_initial;
        u16 curr_count;
        u32 stall_count = 0;
        
        do {
            curr_count = pit_read_counter_verified();
            
            if (curr_count > prev_count) {
                break;
            }
            
            prev_count = curr_count;

            io_busy_wait(10);
            
            stall_count++;
            if (stall_count > 1000000) {
                break;
            }
        } while (curr_count > 0);
        
        tsc_end = rdtsc();
        
        if (curr_count == 0 || stall_count < 1000000) {
            u64 tsc_elapsed = tsc_end - tsc_start;
            u64 freq = (tsc_elapsed * PIT_BASE_FREQ) / PIT_MAX_COUNT;
            if (freq > 100000000ULL && freq < 10000000000ULL) {
                total_tsc += freq;
                valid_measurements++;
            }
        }
        pit_busy_wait(10);
    }
    
    if (valid_measurements == 0) {
        return 2000000;
    }
    
    return (u32)(total_tsc / valid_measurements / 1000);
}

static u32 timer_apic_calibrate_with_tsc(u32 expected_ms) {
    u32 apic_start, apic_end;
    u64 tsc_start, tsc_end;
    u32 total_apic_ticks = 0;
    u32 valid_measurements = 0;
    
    for (int sample = 0; sample < CALIBRATION_LOOPS; sample++) {
        apic_write_reg(LAPIC_TIMER_DIV, TIMER_DIV_16);
        apic_write_reg(LAPIC_TIMER_INITCNT, 0);
        io_busy_wait(100);
        apic_write_reg(LAPIC_TIMER_INITCNT, 0xFFFFFFFFU);
        io_busy_wait(100);
        apic_start = apic_read_reg(LAPIC_TIMER_CURRCNT);
        tsc_start = rdtsc();
        u64 tsc_target = tsc_start + ((u64)g_timer.tsc_khz * expected_ms);
        
        do {
            tsc_end = rdtsc();
            io_busy_wait(1);
        } while (tsc_end < tsc_target);

        apic_end = apic_read_reg(LAPIC_TIMER_CURRCNT);
        apic_write_reg(LAPIC_TIMER_INITCNT, 0);
        u32 apic_elapsed;
        if (apic_start >= apic_end) {
            apic_elapsed = apic_start - apic_end;
        } else {
            apic_elapsed = (0xFFFFFFFFU - apic_end) + apic_start + 1;
        }
        u64 tsc_elapsed = tsc_end - tsc_start;
        u64 expected_apic_ticks = (u64)apic_elapsed;
        if (tsc_elapsed > (u64)g_timer.tsc_khz * expected_ms * 2) {
            u64 est_freq = (u64)apic_elapsed * 1000 / expected_ms;
            while (est_freq < 1000000 && expected_apic_ticks < 0x200000000ULL) {
                expected_apic_ticks += 0x100000000ULL;
                est_freq = expected_apic_ticks * 1000 / expected_ms;
            }
            apic_elapsed = (u32)expected_apic_ticks;
        }
        if (apic_elapsed > 1000 && apic_elapsed < 0x20000000) {
            total_apic_ticks += apic_elapsed;
            valid_measurements++;
        }
        pit_busy_wait(20);
    }
    
    if (valid_measurements == 0) {
        return 0;
    }
    u32 avg_apic_ticks = total_apic_ticks / valid_measurements;
    u32 period = (avg_apic_ticks * APIC_DIVISOR * 1000) / expected_ms;
    
    return period;
}


static bool timer_verify_calibration(void) {
    u32 test_hz = 1000;
    u32 init_val;
    
    if (g_timer.period == 0) return false;

    u64 divisor = (u64)test_hz * APIC_DIVISOR;
    init_val = (u32)((u64)g_timer.period / divisor);
    
    if (init_val < 2 || init_val > 0xFFFFFFFFU) return false;

    apic_write_reg(LAPIC_TIMER_DIV, TIMER_DIV_16);
    apic_write_reg(LAPIC_TIMER_INITCNT, 0);
    io_busy_wait(100);

    u64 start_ticks = g_timer.ticks;

    apic_write_reg(LAPIC_LVT_TIMER, TIMER_VECTOR | TIMER_PERIODIC);
    apic_write_reg(LAPIC_TIMER_INITCNT, init_val);

    u64 tsc_start = rdtsc();
    u64 tsc_target = tsc_start + ((u64)g_timer.tsc_khz * 50);
    
    while (rdtsc() < tsc_target) {
        io_busy_wait(1);
    }

    apic_write_reg(LAPIC_LVT_TIMER, LVT_MASKED);
    apic_write_reg(LAPIC_TIMER_INITCNT, 0);

    u64 elapsed_ticks = g_timer.ticks - start_ticks;
    u32 expected_ticks = test_hz / 20;

    u32 min_ticks = expected_ticks * 95 / 100;
    u32 max_ticks = expected_ticks * 105 / 100;
    
    return (elapsed_ticks >= min_ticks && elapsed_ticks <= max_ticks);
}

void timer_apic_init(u32 desired_freq) {
    if (desired_freq == 0) desired_freq = 1000;

    g_timer.tsc_khz = timer_tsc_calibrate_precise();

    u32 period = 0;
    u32 test_ms[] = {50, 100, 200};
    
    for (int i = 0; i < 3; i++) {
        period = timer_apic_calibrate_with_tsc(test_ms[i]);
        if (period > 0) {
            break;
        }
    }
    
    if (period == 0) {
        g_timer.period = 2000000;
        g_timer.calibrated = false;
    } else {
        g_timer.period = period;
        g_timer.calibrated = true;
    }
    
    g_timer.apic_bus_khz = (u32)((u64)g_timer.period * 1000 / APIC_DIVISOR / 1000);
    if (g_timer.calibrated) {
        timer_verify_calibration();
    }
    u64 divisor = (u64)desired_freq * APIC_DIVISOR;
    u32 init_value;
    
    if (divisor == 0) {
        init_value = 0xFFFFFFFFU;
    } else {
        u64 temp = (u64)g_timer.period / divisor;
        if (temp > 0xFFFFFFFFU) {
            init_value = 0xFFFFFFFFU;
        } else {
            init_value = (u32)temp;
        }
    }
    
    if (init_value < 2) init_value = 2;
    g_timer.init_count = init_value;
    apic_write_reg(LAPIC_TIMER_DIV, TIMER_DIV_16);
    apic_write_reg(LAPIC_LVT_TIMER, TIMER_VECTOR | TIMER_PERIODIC);
    apic_write_reg(LAPIC_TIMER_INITCNT, init_value);
    ioapic_redirect_irq(0,
			32,
			apic_get_id(),
			IOAPIC_FLAG_EDGE_TRIGGERED | IOAPIC_FLAG_ACTIVE_HIGH);
    ioapic_unmask_irq(0);
}

void timer_apic_handler(void) {
    g_timer.ticks++;
    if (g_timer.ticks >= 1000) {
	    g_timer.ticks = 0;
	    seconds++;
	    clock_tick();
    }

    static u32 preempt_counter = 0;
    if (++preempt_counter >= 10) {
        preempt_counter = 0;
        need_reschedule = 1;
    }

    static u32 last_cursor = 0;
    if (g_timer.ticks - last_cursor >= 500) {
        last_cursor = g_timer.ticks;
        terminal_update_cursor();
    }

    apic_eoi();
}

u64 timer_apic_ticks(void) {
    return g_timer.ticks;
}

u32 timer_apic_freq(void) {
    return g_timer.apic_bus_khz * 1000;
}

u32 timer_apic_current_freq(void) {
    if (g_timer.init_count == 0) return 0;
    u64 freq = (u64)g_timer.period / ((u64)g_timer.init_count * APIC_DIVISOR);
    
    if (freq > 0xFFFFFFFFU) freq = 0xFFFFFFFFU;
    
    return (u32)freq;
}

u32 timer_apic_ms(void) {
    u32 current_freq = timer_apic_current_freq();
    if (current_freq == 0) return 0;
    
    return (u32)(g_timer.ticks * 1000 / current_freq);
}

u32 timer_apic_ticks_per_ms(void) {
    return g_timer.apic_bus_khz;
}

u64 timer_tsc_freq(void) {
    return (u64)g_timer.tsc_khz * 1000;
}
