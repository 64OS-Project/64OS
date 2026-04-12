// kernel/smp.c
#include <kernel/smp.h>
#include <acpi.h>
#include <apic.h>
#include <asm/cpu.h>
#include <mm/pmm.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <kernel/timer.h>
#include <kernel/findcpus.h>

cpu_info_t cpus[MAX_CPUS] = {0};
u32 cpu_count = 0;
u32 boot_cpu_id = 0;
volatile u32 ap_ready = 0;

//Startup code for AP (will be copied to low memory)
static u8 ap_startup_code[] = {
    0xFA,                    // cli
    0xF4,                    // hlt
    0xEB, 0xFC               //jmp $-2 (endless loop)
};

static void start_ap(u32 apic_id, u32 cpu_index) {
    (void)cpu_index;
    
    //Copy the startup code to address 0x8000
    u64 startup_addr = 0x8000;
    memcpy((void*)startup_addr, ap_startup_code, sizeof(ap_startup_code));
    
    //Send INIT IPI
    apic_send_init(apic_id);
    timer_mdelay(10);
    
    //Send STARTUP IPI
    apic_send_startup(apic_id, (startup_addr >> 12) & 0xFF);
    timer_mdelay(10);
    
    //Second STARTUP for reliability
    apic_send_startup(apic_id, (startup_addr >> 12) & 0xFF);
    
    cpus[cpu_index].online = true;
    terminal_success_printf("[SMP] CPU %d (APIC %u) started (HLT loop)\n", 
                           cpu_index, apic_id);
}

static void smp_scan_madt(void) {
    acpi_t *acpi = acpi_get_table();
    if (!acpi || !acpi->madt) {
        terminal_warn_printf("[SMP] No MADT, single core only\n");
        cpu_count = 1;
        cpus[0].apic_id = apic_get_id();
        cpus[0].online = true;
        return;
    }
    
    u8 *entry = (u8*)acpi->madt + sizeof(madt_t);
    u8 *end = (u8*)acpi->madt + acpi->madt->header.length;
    u32 bsp_apic = apic_get_id();
    
    while (entry < end && cpu_count < MAX_CPUS) {
        madt_entry_header_t *header = (madt_entry_header_t*)entry;
        
        if (header->type == MADT_TYPE_LOCAL_APIC) {
            madt_local_apic_t *lapic = (madt_local_apic_t*)entry;
            
            cpus[cpu_count].apic_id = lapic->apic_id;
            cpus[cpu_count].acpi_id = lapic->acpi_processor_id;
            cpus[cpu_count].cpu_index = cpu_count;
            cpus[cpu_count].enabled = (lapic->flags & 1) != 0;
            cpus[cpu_count].online = false;
            
            if (lapic->apic_id == bsp_apic) {
                cpus[cpu_count].online = true;
                boot_cpu_id = cpu_count;
                terminal_printf("[SMP] CPU %d: BSP (APIC %u)\n", cpu_count, lapic->apic_id);
            } else {
                terminal_printf("[SMP] CPU %d: AP (APIC %u)\n", cpu_count, lapic->apic_id);
            }
            cpu_count++;
        }
        
        entry += header->length;
    }

    u32 real_cpu_count = get_cpu_count();
    if (real_cpu_count != cpu_count) {
        terminal_warn_printf("[SMP] MADT contents incorrect CPU count. Using real count.");
        cpu_count = real_cpu_count;
    }
    
    //Move BSP to the beginning
    if (boot_cpu_id != 0) {
        cpu_info_t tmp = cpus[0];
        cpus[0] = cpus[boot_cpu_id];
        cpus[boot_cpu_id] = tmp;
        boot_cpu_id = 0;
    }
    
    terminal_success_printf("[SMP] Found %u CPUs\n", cpu_count);
}

void smp_init(void) {
    terminal_printf("[SMP] Initializing...\n");
    
    smp_scan_madt();
    
    if (cpu_count <= 1) {
        terminal_printf("[SMP] Single CPU system\n");
        return;
    }
    
    //Launch all APs (other kernels)
    for (u32 i = 1; i < cpu_count; i++) {
        if (cpus[i].enabled) {
            start_ap(cpus[i].apic_id, i);
            timer_mdelay(20);  //short pause between starts
        }
    }
    
    terminal_success_printf("[SMP] %u CPUs online (APs in HLT loop)\n", cpu_count);
}
