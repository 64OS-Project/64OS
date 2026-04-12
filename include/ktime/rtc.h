#ifndef RTC_H
#define RTC_H

#include <kernel/types.h>

#define TIMEZONE_OFFSET 7

void read_rtc_time(u32 *hour, u32 *minute, u32 *second);
void read_rtc_date(u32 *year, u32 *month, u32 *day);

#endif