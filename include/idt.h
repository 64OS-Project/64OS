#ifndef IDT_H
#define IDT_H
#include <kernel/types.h>

#define IDT_ENTRIES 256

#define TIMER 32
#define KEYBOARD 33

#define KERNEL_CODE_SEL 0x08
#define IDT_GATE_INT 0x8E     /*
 * interrupt gate
 */
#define IDT_GATE_SYSCALL 0xEE /*
 * syscall gate (DPL=3)
 */

/*
 * 64-bit IDT entry
 */
struct __attribute__((packed)) idt_entry
{
    u16 offset_low;  /*
 * bits 0..15
 */
    u16 selector;    /*
 * code segment selector
 */
    u8 ist;          /*
 * IST (3 bits) + zero
 */
    u8 type_attr;    /*
 * type and attributes
 */
    u16 offset_mid;  /*
 * bits 16..31
 */
    u32 offset_high; /*
 * bits 32..63
 */
    u32 zero;        /*
 * reserved
 */
};

/*
 * lidt pointer for 64-bit - 16-byte base
 */
struct __attribute__((packed)) idt_ptr
{
    u16 limit;
    u64 base;
};

void idt_set_gate(u8 num, void (*handler)(), u16 sel, u8 flags, u8 ist);
void idt_install(void);

#endif /*
 * IDT_H
 */
