// include/kernel/smp.h
#ifndef SMP_H
#define SMP_H

#include <kernel/types.h>

#define MAX_CPUS 32

typedef struct {
    u32 apic_id;
    u32 acpi_id;
    u32 cpu_index;
    bool enabled;
    bool online;
} cpu_info_t;

extern cpu_info_t cpus[MAX_CPUS];
extern u32 cpu_count;
extern u32 boot_cpu_id;

void smp_init(void);

#endif
