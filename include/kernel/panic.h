#ifndef PANIC_H
#define PANIC_H

#include <kernel/types.h>

typedef struct
{
    u64 rax, rbx, rcx, rdx;
    u64 rsi, rdi, rbp, rsp;
    u64 r8, r9, r10, r11;
    u64 r12, r13, r14, r15;
    u64 rip, rflags;

    u16 cs, ds, es, fs, gs, ss;

    u64 cr0, cr2, cr3, cr4;
} registers_state;

void panic(const char *stop);

#endif
