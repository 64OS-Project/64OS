#ifndef CLOCK_H
#define CLOCK_H

#include <kernel/types.h>

typedef struct
{
    u8 hh;
    u8 mm;
    u8 ss;
    u32 epoch;
} ClockTime;

extern volatile ClockTime system_clock;

void clock_tick();
void format_clock(char *buffer, ClockTime t);
void init_system_clock(void);

#endif
