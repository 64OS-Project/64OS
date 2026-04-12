#include <idt.h>
#include <asm/io.h>
#include <asm/cpu.h>
#include <isr.h>
#include <apic.h>
#include <kernel/types.h>

static struct idt_entry idt[IDT_ENTRIES];
struct idt_ptr idtp;

struct {
    u16 limit;
    u64 base;
} GDT64Pointer64;

struct {
    u16 limit;
    u64 base;
} idtPtr;

void idt_set_gate(u8 num, void (*handler)(), u16 sel, u8 flags, u8 ist)
{
    if (num >= IDT_ENTRIES)
        return;

    u64 base = (u64)handler;
    idt[num].offset_low = (u16)(base & 0xFFFF);
    idt[num].selector = sel;
    idt[num].ist = ist & 0x07;
    idt[num].type_attr = flags;
    idt[num].offset_mid = (u16)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (u32)((base >> 32) & 0xFFFFFFFF);
    idt[num].zero = 0;
}

void idt_install(void)
{
    idtp.limit = (u16)(sizeof(idt) - 1);
    idtp.base = (u64)&idt;

    static const void (*stubs[32])() = {
        isr_stub_0, isr_stub_1, isr_stub_2, isr_stub_3,
        isr_stub_4, isr_stub_5, ud, isr_stub_7,
        df, isr_stub_9, isr_stub_10, isr_stub_11,
        isr_stub_12, gpf, pf, isr_stub_15,
        isr_stub_16, isr_stub_17, mc, isr_stub_19,
        isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
        isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
        isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
    };

    /*
 * CPU exceptions (0-31)
 */
    for (int i = 0; i < 32; ++i)
    {
        idt_set_gate(i, stubs[i], KERNEL_CODE_SEL, IDT_GATE_INT, 0);
    }

    idt_set_gate(32, timer_isr, KERNEL_CODE_SEL, IDT_GATE_INT, 0);
    idt_set_gate(33, kbd_isr, KERNEL_CODE_SEL, IDT_GATE_INT, 0);;

    /*
 * Loading IDT
 */
    ldit(&idtp);
}
