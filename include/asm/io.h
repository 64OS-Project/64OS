#ifndef IO_H
#define IO_H

#include <kernel/types.h>

static inline u8 inb(u16 port)
{
    u8 ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(u16 port, u8 data)
{
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

static inline u16 inw(u16 port)
{
    u16 ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(u16 port, u16 data)
{
    asm volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}

static inline u32 inl(u16 port)
{
    u32 ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(u16 port, u32 data)
{
    asm volatile("outl %0, %1" : : "a"(data), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);  // Write to unused port 0x80 (POST card)
}

#endif
