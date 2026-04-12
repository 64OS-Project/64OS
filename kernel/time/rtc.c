#include <ktime/rtc.h>
#include <asm/cpu.h>
#include <asm/io.h>

static void rtc_wait_ready(void)
{
    while (1)
    {
        outb(0x70, 0x0A);
        if (!(inb(0x71) & 0x80))
            break;
    }
}

static u8 cmos_read(u8 reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static u8 bcd_to_bin(u8 bcd)
{
    return (bcd & 0x0F) + (bcd >> 4) * 10;
}

void read_rtc_time(u32 *hour, u32 *minute, u32 *second)
{
    rtc_wait_ready();

    u8 ss = cmos_read(0x00);
    u8 mm = cmos_read(0x02);
    u8 hh = cmos_read(0x04);
    u8 regB = cmos_read(0x0B);

    u8 is_pm = hh & 0x80;

    if (!(regB & 0x04))
    {
        ss = bcd_to_bin(ss);
        mm = bcd_to_bin(mm);
        hh = bcd_to_bin(hh & 0x7F);
    }
    else
    {
        hh &= 0x7F;
    }

    if (!(regB & 0x02) && is_pm)
    {
        hh = (hh + 12) % 24;
    }

    hh = (u8)((hh + TIMEZONE_OFFSET + 24) % 24);

    *hour = hh;
    *minute = mm;
    *second = ss;
}

void read_rtc_date(u32 *year, u32 *month, u32 *day)
{
    rtc_wait_ready();

    u8 day_reg = cmos_read(0x07);
    u8 month_reg = cmos_read(0x08);
    u8 year_reg = cmos_read(0x09);
    u8 century_reg = cmos_read(0x32);
    u8 regB = cmos_read(0x0B);

    if (!(regB & 0x04))
    {
        day_reg = bcd_to_bin(day_reg);
        month_reg = bcd_to_bin(month_reg);
        year_reg = bcd_to_bin(year_reg);
        century_reg = bcd_to_bin(century_reg);
    }

    *day = day_reg;
    *month = month_reg;
    
    if (century_reg >= 19) {
        *year = century_reg * 100 + year_reg;
    } else {
        *year = 2000 + year_reg;
    }
}
