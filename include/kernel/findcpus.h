#ifndef FINDCPUS_H
#define FINDCPUS_H

#include <kernel/types.h>

#define FINDCPU_MAX_CPUS 256

/*
 * Information about each CPU
 */
typedef struct {
    u32 lapic_id;           /*
 * Local APIC ID (from CPUID or MP table)
 */
    u32 apic_version;       /*
 * LAPIC version (from register)
 */
    u32 processor_id;       /*
 * Processor ID (from CPUID)
 */
    bool enabled;           /*
 * Included/Available
 */
    bool bsp;               /*
 * Boot Processor?
 */
    u32 acpi_id;            /*
 * ACPI ID (if available)
 */
} findcpu_info_t;

/*
 * Global system information
 */
typedef struct {
    u32 total_logical_cpus;     /*
 * Total logical processors
 */
    u32 total_cores;            /*
 * Total physical cores
 */
    u32 threads_per_core;       /*
 * Threads per core (SMT)
 */
    u32 packages;               /*
 * Physical processors
 */
    bool smt_enabled;           /*
 * Is SMT/Hyper-Threading enabled?
 */
    bool topology_valid;        /*
 * Topology defined correctly
 */
    findcpu_info_t cpus[FINDCPU_MAX_CPUS];  /*
 * CPU array
 */
    u32 cpu_count;              /*
 * Number of detected CPUs
 */
} system_topology_t;

/*
 * Functions
 */
void find_cpus_detect(system_topology_t *topology);
void find_cpus_print_info(system_topology_t *topology);
u32 find_cpus_get_bsp_id(void);
const char* find_cpus_get_topology_string(system_topology_t *topology);
u32 get_cpu_count(void);        /*
 * Simple function to get the number of CPUs
 */

#endif
