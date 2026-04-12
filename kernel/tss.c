#include <kernel/tss.h>
#include <kernel/panic.h>
#include <kernel/types.h>
#include <mm/heap.h>

extern char _tss_buffer[];
extern u64 gdt[];
extern char _stack_end[];

static tss_t *tss = (tss_t*)_tss_buffer;

void tss_init(void)
{
    if (!tss)
    {
        panic("TSS_ALLOCATION_FAILED");
    }

    /*
 * Clearing TSS
 */
    for (int i = 0; i < sizeof(tss_t); i++)
        ((u8 *)tss)[i] = 0;

    /*
 * Initialize rsp0 from the initial kernel stack
 */
    tss->rsp0 = (u64)&_stack_end;

    if (tss->rsp0 == 0)
    {
        panic("TSS_INVALID_RSP0");
    }

    tss->iopb_offset = sizeof(tss_t);

    /*
 * We form a 64-bit TSS descriptor according to the Intel manual
 */
    u64 tss_addr = (u64)tss;
    u16 tss_limit = sizeof(tss_t) - 1;

    /*
 * Lower 8 bytes: limit(16) | base_low(24) | access(8) | flags(4) | base_mid(16)
 */
    u64 lower = 0;
    lower |= ((u64)tss_limit & 0xFFFF);        /*
 * bits 0-15: limit
 */
    lower |= (((tss_addr) & 0xFFFFFF) << 16);       /*
 * bits 16-39: base_low
 */
    lower |= (0x89ULL << 40);                       /*
 * bits 40-47: access (P=1, DPL=0, Type=9)
 */
    lower |= ((((tss_addr) >> 24) & 0xFFFF) << 48); /*
 * bits 48-63: base_mid
 */

    /*
 * Upper 8 bytes: base_high(32) | reserved(32)
 */
    u64 upper = (tss_addr >> 40) & 0xFFFFFFFF;

    gdt[5] = lower;
    gdt[6] = upper;

    /*
 * Loading TSS
 */
    asm volatile("mov $0x28, %%ax; ltr %%ax" ::: "ax");
}

void tss_update_rsp0(u64 rsp0)
{
    if (tss)
        tss->rsp0 = rsp0;
}
