#ifndef ASM_CPU_H
#define ASM_CPU_H

#include <kernel/types.h>

static inline void intd(void) {
    asm volatile ("cli" : : : "memory");
}

static inline void inte(void) {
    asm volatile ("sti" : : : "memory");
}

static inline void halt(void) {
    asm volatile ("hlt");
}

static inline u64 rdtsc(void) {
    u32 lo, hi;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

static inline u64 read_cr0(void) {
    u64 val;
    asm volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

static inline void write_cr0(u64 val) {
    asm volatile("mov %0, %%cr0" : : "r"(val) : "memory");
}

static inline u64 read_cr3(void) {
    u64 val;
    asm volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void write_cr3(u64 val) {
    asm volatile("mov %0, %%cr3" : : "r"(val) : "memory");
}

static inline u64 read_cr4(void) {
    u64 val;
    asm volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

static inline void write_cr4(u64 val) {
    asm volatile("mov %0, %%cr4" : : "r"(val) : "memory");
}

static inline u64 read_rflags(void) {
    u64 flags;
    asm volatile("pushfq; popq %0" : "=g"(flags));
    return flags;
}

static inline void write_rflags(u64 flags) {
    asm volatile("pushq %0; popfq" : : "g"(flags) : "memory", "cc");
}

static inline void invlpg(u64 addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline void ldit(void* ptr) {
    asm volatile("lidt (%0)" : : "r"(ptr));
}

static inline void ldgt(void* ptr) {
    asm volatile("lgdt (%0)" : : "r"(ptr));
}

static inline u64 rdmsr(u32 msr) {
    u32 lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static inline void wrmsr(u32 msr, u64 val) {
    u32 lo = (u32)val;
    u32 hi = (u32)(val >> 32);
    asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static inline void cpuid(u32 code, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    asm volatile("cpuid"
                 : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                 : "a"(code), "c"(0));
}

static inline void cpuid_leaf(u32 code, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    asm volatile("cpuid"
                 : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                 : "a"(code), "c"(subleaf));
}

static inline void cpuchill(void) {
    asm volatile("pause");
}

static inline bool check_interrupt_status(void) {
    u64 flags;
    asm volatile("pushfq; popq %0" : "=g"(flags));
    return (flags & 0x200) ? true : false;
}

#endif /*
 * ASM_CPU_H
 */