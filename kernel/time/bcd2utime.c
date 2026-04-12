#include <kernel/timer.h>

u8 bcd_to_bin(u8 bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

u8 bin_to_bcd(u8 bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

static bool is_leap(u32 year) {
    return (year % 400 == 0) || (year % 4 == 0 && year % 100 != 0);
}

static u8 days_in_month(u32 year, u8 month) {
    static const u8 days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && is_leap(year)) return 29;
    return days[month-1];
}

unix_time_t time_to_unix(datetime_t *t) {
    if (!t) return 0;
    
    u64 days = 0;
    for (u32 y = 1970; y < t->year; y++)
        days += is_leap(y) ? 366 : 365;
    
    for (u8 m = 1; m < t->month; m++)
        days += days_in_month(t->year, m);
    
    days += (t->day - 1);
    
    return days * 86400ULL + t->hour * 3600 + t->minute * 60 + t->second;
}

void unix_to_time(unix_time_t ut, datetime_t *t) {
    if (!t) return;
    
    u32 days = ut / 86400;
    u32 rem = ut % 86400;
    
    t->hour = rem / 3600;
    t->minute = (rem % 3600) / 60;
    t->second = rem % 60;
    
    u32 year = 1970;
    while (days >= (is_leap(year) ? 366 : 365)) {
        days -= is_leap(year) ? 366 : 365;
        year++;
    }
    t->year = year;
    
    for (t->month = 1; t->month <= 12; t->month++) {
        u8 dim = days_in_month(year, t->month);
        if (days < dim) {
            t->day = days + 1;
            break;
        }
        days -= dim;
    }
}