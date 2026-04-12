#ifndef TIMER_H
#define TIMER_H

#include <kernel/types.h>

/*
 * ============================================================================= Time structures ============================================================================
 */

typedef struct {
    u32 year;
    u8  month;
    u8  day;
    u8  hour;
    u8  minute;
    u8  second;
} datetime_t;

typedef u64 unix_time_t;

/*
 * ============================================================================= BCD / Unix time conversion ============================================================================
 */

u8 bcd_to_bin(u8 bcd);
u8 bin_to_bcd(u8 bin);
unix_time_t time_to_unix(datetime_t *t);
void unix_to_time(unix_time_t ut, datetime_t *t);

/*
 * ============================================================================= RTC ============================================================================
 */

bool rtc_read_time(datetime_t *t);

/*
 * ============================================================================= APIC Timer ============================================================================
 */

void timer_apic_init(u32 freq);
void timer_apic_handler(void);
u64 timer_apic_ticks(void);
u32 timer_apic_freq(void);
u32 timer_apic_ms(void);
u32 timer_apic_ticks_per_ms(void);
u64 timer_tsc_freq(void);

/*
 * ============================================================================= Delay functions (TSC-based) ============================================================================
 */

void timer_udelay(u32 us);
void timer_mdelay(u32 ms);
void timer_sdelay(u32 s);
void timer_sleep(u32 ms);

#endif