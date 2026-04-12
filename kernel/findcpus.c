#include <kernel/findcpus.h>
#include <asm/cpu.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <apic.h>

/*
 * Global variable for storing topology (for get_cpu_count)
 */
static system_topology_t g_system_topology = {0};
static bool g_topology_initialized = false;

/*
 * Helper function for extracting bits
 */
static inline u32 extract_bits(u32 value, u32 high, u32 low) {
    return (value >> low) & ((1 << (high - low + 1)) - 1);
}

/*
 * Step 1: Check CPUID leaf 0x0B support
 */
static bool supports_leaf_0xB(void) {
    u32 max_leaf;
    u32 dummy;
    
    cpuid(0, &max_leaf, &dummy, &dummy, &dummy);
    
    return (max_leaf >= 0x0B);
}

/*
 * Getting the LAPIC ID of the current CPU
 */
static u32 get_current_lapic_id(void) {
    u32 eax, ebx, ecx, edx;
    
    /*
 * CPUID leaf 0x0B, ECX=0 gives APIC ID in EDX[31:24]
 */
    if (supports_leaf_0xB()) {
        cpuid_leaf(0x0B, 0, &eax, &ebx, &ecx, &edx);
        return (edx >> 24) & 0xFF;
    }
    
    /*
 * Fallback: leaf 0x01
 */
    cpuid(0x01, &eax, &ebx, &ecx, &edx);
    return (ebx >> 24) & 0xFF;
}

/*
 * Step 1a: Legacy method (for older CPUs without leaf 0x0B)
 */
static void detect_legacy(system_topology_t *topology) {
    u32 eax, ebx, ecx, edx;
    
    /*
 * CPUID leaf 0x01: LogicalProcessorsPerPackage
 */
    cpuid(0x01, &eax, &ebx, &ecx, &edx);
    
    /*
 * EBX[23:16] = Logical processors per package
 */
    u32 logical_per_package = extract_bits(ebx, 23, 16);
    if (logical_per_package == 0) logical_per_package = 1;
    
    /*
 * CPUID leaf 0x04, ECX=0: Cores per package
 */
    cpuid_leaf(0x04, 0, &eax, &ebx, &ecx, &edx);
    
    /*
 * EAX[31:26] = Cores per package (APIC ID shift count)
 */
    u32 cores_per_package = extract_bits(eax, 31, 26);
    
    /*
 * If cores_per_package == 0, then there is 1 core
 */
    if (cores_per_package == 0) {
        cores_per_package = 1;
    }
    
    topology->total_cores = cores_per_package;
    topology->total_logical_cpus = logical_per_package;
    topology->threads_per_core = logical_per_package / cores_per_package;
    topology->smt_enabled = (logical_per_package > cores_per_package);
    topology->packages = 1; /*
 * Legacy method does not detect packages
 */
    topology->topology_valid = true;
    
    terminal_debug_printf("[CPUDETECT] Legacy: %u cores, %u logical CPUs, SMT=%s\n",
                         cores_per_package, logical_per_package,
                         topology->smt_enabled ? "yes" : "no");
}

/*
 * Step 2: Extended Topology Enumeration (leaf 0x0B)
 */
static void detect_extended_topology(system_topology_t *topology) {
    u32 eax, ebx, ecx, edx;
    u32 smt_level_cores = 0;
    u32 core_level_cores = 0;
    u32 threads_per_core = 1;
    u32 cores_per_package = 1;
    
    /*
 * Level 0: SMT (streams)
 */
    cpuid_leaf(0x0B, 0, &eax, &ebx, &ecx, &edx);
    smt_level_cores = ebx & 0xFFFF;  /*
 * Number of logical processors at this level
 */
    
    /*
 * Level 1: Core
 */
    cpuid_leaf(0x0B, 1, &eax, &ebx, &ecx, &edx);
    core_level_cores = ebx & 0xFFFF;  /*
 * Number of logical processors at this level
 */
    
    /*
 * If core_level_cores == 0, then there is no second level (one core)
 */
    if (core_level_cores == 0) {
        core_level_cores = 1;
        threads_per_core = smt_level_cores;
        cores_per_package = 1;
    } else {
        threads_per_core = smt_level_cores;
        cores_per_package = core_level_cores / smt_level_cores;
    }
    
    topology->total_cores = cores_per_package;
    topology->total_logical_cpus = core_level_cores;
    topology->threads_per_core = threads_per_core;
    topology->smt_enabled = (threads_per_core > 1);
    topology->packages = 1; /*
 * For simplicity, we consider one package
 */
    topology->topology_valid = true;
    
    terminal_debug_printf("[CPUDETECT] Extended: %u cores, %u logical CPUs, %u threads/core, SMT=%s\n",
                         cores_per_package, core_level_cores, threads_per_core,
                         topology->smt_enabled ? "yes" : "no");
}

/*
 * Main CPU detection function
 */
void find_cpus_detect(system_topology_t *topology) {
    if (!topology) return;
    
    memset(topology, 0, sizeof(system_topology_t));
    
    terminal_info_printf("[CPUDETECT] Detecting CPU topology...\n");
    
    /*
 * Determining the detection method
 */
    if (supports_leaf_0xB()) {
        terminal_debug_printf("[CPUDETECT] Using Extended Topology (leaf 0x0B)\n");
        detect_extended_topology(topology);
    } else {
        terminal_debug_printf("[CPUDETECT] Using Legacy method (leaf 0x01/0x04)\n");
        detect_legacy(topology);
    }
    
    /*
 * Fill in information about each CPU (simplified - BSP only)
 */
    topology->cpu_count = topology->total_logical_cpus;
    if (topology->cpu_count > FINDCPU_MAX_CPUS) {
        topology->cpu_count = FINDCPU_MAX_CPUS;
    }
    
    /*
 * Fill in BSP (current CPU)
 */
    topology->cpus[0].lapic_id = get_current_lapic_id();
    topology->cpus[0].apic_version = apic_get_version();
    topology->cpus[0].processor_id = 0;
    topology->cpus[0].enabled = true;
    topology->cpus[0].bsp = true;
    
    /*
 * For the rest of the CPUs, for now we just fill in the stubs
 */
    for (u32 i = 1; i < topology->cpu_count; i++) {
        topology->cpus[i].lapic_id = i;  /*
 * TODO: real APIC IDs from MADT/MP table
 */
        topology->cpus[i].enabled = true;
        topology->cpus[i].bsp = false;
    }
    
    g_system_topology = *topology;
    g_topology_initialized = true;
    
    terminal_success_printf("[CPUDETECT] Detected %u logical CPUs, %u physical cores\n",
                           topology->total_logical_cpus, topology->total_cores);
}

/*
 * Displaying topology information
 */
void find_cpus_print_info(system_topology_t *topology) {
    if (!topology || !topology->topology_valid) {
        terminal_error_printf("[CPUDETECT] No valid topology information\n");
        return;
    }
    
    terminal_printf("\n=== CPU Topology ===\n");
    terminal_printf("Logical CPUs:   %u\n", topology->total_logical_cpus);
    terminal_printf("Physical cores: %u\n", topology->total_cores);
    terminal_printf("Threads/core:   %u\n", topology->threads_per_core);
    terminal_printf("Packages:       %u\n", topology->packages);
    terminal_printf("SMT enabled:    %s\n", topology->smt_enabled ? "Yes" : "No");
    terminal_printf("Method:         %s\n", supports_leaf_0xB() ? "Extended (0x0B)" : "Legacy");
    
    terminal_printf("\nCPU List:\n");
    for (u32 i = 0; i < topology->cpu_count && i < FINDCPU_MAX_CPUS; i++) {
        terminal_printf("  CPU%u: APIC ID=%u, %s\n", 
                       i, 
                       topology->cpus[i].lapic_id,
                       topology->cpus[i].bsp ? "BSP" : "AP");
    }
    terminal_printf("==================\n\n");
}

/*
 * Get BSP ID
 */
u32 find_cpus_get_bsp_id(void) {
    if (!g_topology_initialized) {
        return 0;
    }
    
    for (u32 i = 0; i < g_system_topology.cpu_count; i++) {
        if (g_system_topology.cpus[i].bsp) {
            return g_system_topology.cpus[i].lapic_id;
        }
    }
    
    return get_current_lapic_id();
}

/*
 * Get a string representation of the topology
 */
const char* find_cpus_get_topology_string(system_topology_t *topology) {
    static char buffer[128];
    
    if (!topology || !topology->topology_valid) {
        return "Unknown";
    }
    
    snprintf(buffer, sizeof(buffer), "%uC/%uT (%u logical, SMT=%s)",
             topology->total_cores,
             topology->threads_per_core,
             topology->total_logical_cpus,
             topology->smt_enabled ? "on" : "off");
    
    return buffer;
}

/*
 * SIMPLE FUNCTION TO GET THE NUMBER OF CPU
 */
u32 get_cpu_count(void) {
    /*
 * If the topology is already initialized, return from it
 */
    if (g_topology_initialized) {
        return g_system_topology.total_logical_cpus;
    }
    
    /*
 * Otherwise we detect on the fly
 */
    system_topology_t temp_topology;
    find_cpus_detect(&temp_topology);
    
    return temp_topology.total_logical_cpus;
}

/*
 * Additional function: get the number of physical cores
 */
u32 get_core_count(void) {
    if (g_topology_initialized) {
        return g_system_topology.total_cores;
    }
    
    system_topology_t temp_topology;
    find_cpus_detect(&temp_topology);
    
    return temp_topology.total_cores;
}

/*
 * Check if SMT/Hyper-Threading is enabled
 */
bool is_smt_enabled(void) {
    if (g_topology_initialized) {
        return g_system_topology.smt_enabled;
    }
    
    system_topology_t temp_topology;
    find_cpus_detect(&temp_topology);
    
    return temp_topology.smt_enabled;
}
