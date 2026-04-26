#include <kernel/timer.h>
#include <asm/io.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define CMOS_SEC   0x00
#define CMOS_MIN   0x02
#define CMOS_HOUR  0x04
#define CMOS_DAY   0x07
#define CMOS_MONTH 0x08
#define CMOS_YEAR  0x09
#define CMOS_CENT  0x32
#define CMOS_STAT_B 0x0B

static u8 cmos_read(u8 reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

bool rtc_read_time(datetime_t *t) {
    if (!t) return false;
    
    u8 b = cmos_read(CMOS_STAT_B);
    bool bcd = !(b & 0x04);
    
    t->second = cmos_read(CMOS_SEC);
    t->minute = cmos_read(CMOS_MIN);
    t->hour   = cmos_read(CMOS_HOUR);
    t->day    = cmos_read(CMOS_DAY);
    t->month  = cmos_read(CMOS_MONTH);
    t->year   = cmos_read(CMOS_YEAR);
    u8 cent = cmos_read(CMOS_CENT);
    
    if (bcd) {
        t->second = bcd_to_bin(t->second);
        t->minute = bcd_to_bin(t->minute);
        t->hour   = bcd_to_bin(t->hour);
        t->day    = bcd_to_bin(t->day);
        t->month  = bcd_to_bin(t->month);
        t->year   = bcd_to_bin(t->year);
        cent      = bcd_to_bin(cent);
    }
    
    t->year += (cent ? cent * 100 : 2000);
    return true;
}