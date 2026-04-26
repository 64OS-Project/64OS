#include <ktime/clock.h>
#include <ktime/rtc.h>

volatile ClockTime system_clock = {0, 0, 0}; //start from 00:00:00

static const u8 days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int is_leap_year(int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static u32 rtc_to_epoch(u32 year, u32 month, u32 day,
                             u32 hour, u32 minute, u32 second) {
    u32 epoch = 0;
    
    for (u32 y = 1970; y < year; y++) {
        epoch += is_leap_year(y) ? 366 : 365;
    }
    
    for (u32 m = 1; m < month; m++) {
        epoch += days_in_month[m-1];
        if (m == 2 && is_leap_year(year)) epoch++;
    }
    
    epoch += day - 1;
    
    epoch = epoch * 86400 + hour * 3600 + minute * 60 + second;
    
    return epoch;
}

void init_system_clock(void)
{
    u32 h, m, s;
    read_rtc_time(&h, &m, &s);
    
    u32 year, month, day;
    read_rtc_date(&year, &month, &day);
    
    system_clock.hh = (u8)h;
    system_clock.mm = (u8)m;
    system_clock.ss = (u8)s;
    system_clock.epoch = rtc_to_epoch(year, month, day, h, m, s);
}

void clock_tick()
{
    system_clock.ss++;
    system_clock.epoch++;

    if (system_clock.ss >= 60)
    {
        system_clock.ss = 0;
        system_clock.mm++;

        if (system_clock.mm >= 60)
        {
            system_clock.mm = 0;
            system_clock.hh++;

            if (system_clock.hh >= 24)
            {
                system_clock.hh = 0;
            }
        }
    }
}

void format_clock(char *buffer, ClockTime t)
{
    buffer[0] = '0' + (t.hh / 10);
    buffer[1] = '0' + (t.hh % 10);
    buffer[2] = ':';
    buffer[3] = '0' + (t.mm / 10);
    buffer[4] = '0' + (t.mm % 10);
    buffer[5] = ':';
    buffer[6] = '0' + (t.ss / 10);
    buffer[7] = '0' + (t.ss % 10);
    buffer[8] = '\0';
}
