#ifndef TSS_H
#define TSS_H

#include <kernel/types.h>

/*
 * TSS structure for 64-bit mode
 */
typedef struct
{
    u32 reserved0;
    u64 rsp0; /*
 * Stack pointer for ring 0
 */
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iopb_offset;
} __attribute__((packed)) tss_t;

/*
 * Initializes the TSS and writes the handle to the GDT
 */
void tss_init(void);

/*
 * Updates RSP0 in TSS on context switch
 */
void tss_update_rsp0(u64 rsp0);

#endif
