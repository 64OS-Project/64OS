#include <kernel/mc.h>
#include <kernel/panic.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <asm/cpu.h>
#include <kernel/sched.h>

#define IA32_MCI_STATUS_ADDRV    (1ULL << 58)
#define IA32_MCI_STATUS_MISCV    (1ULL << 59)
#define IA32_MCI_STATUS_EN       (1ULL << 60)
#define IA32_MCI_STATUS_UC       (1ULL << 61)
#define IA32_MCI_STATUS_OVER     (1ULL << 62)
#define IA32_MCI_STATUS_VAL      (1ULL << 63)

#define IA32_MCI_STATUS_PCC      (1ULL << 55)  /*
 * Processor Context Corrupt
 */

/*
 * Reading MCA Registers
 */
void read_mca_registers(mc_info_t *info) {
    u32 i;
    u64 mcg_cap = rdmsr(MSR_IA32_MCG_CAP);
    
    info->mcg_cap = mcg_cap;
    info->mcg_status = rdmsr(MSR_IA32_MCG_STATUS);
    info->num_banks = mcg_cap & 0xFF;
    
    if (info->num_banks > 32) info->num_banks = 32;
    
    /*
 * Looking for the first failed bank
 */
    for (i = 0; i < info->num_banks; i++) {
        u64 status = rdmsr(0x400 + i);
        
        if (status & IA32_MCI_STATUS_VAL) {
            info->bank_status = status;
            info->bank_addr = rdmsr(0x401 + i);
            info->bank_misc = rdmsr(0x402 + i);
            break;
        }
    }
    
    /*
 * Analyzing MCG_STATUS
 */
    info->pcc = !!(info->mcg_status & MCG_STATUS_PCC);
    info->ripv = !!(info->mcg_status & MCG_STATUS_RIPV);
    info->eipv = !!(info->mcg_status & MCG_STATUS_EIPV);
    info->mcip = !!(info->mcg_status & MCG_STATUS_MCIP);
}

/*
 * Definition of severity
 */
mc_severity_t determine_mc_severity(mc_info_t *info) {
    /*
 * PCC = 1 → processor state is unpredictable → fatal
 */
    if (info->pcc) {
        info->severity_string = "FATAL (PCC=1)";
        return MC_SEVERITY_FATAL;
    }
    
    /*
 * Uncorrected error
 */
    if (info->bank_status & IA32_MCI_STATUS_UC) {
        /*
 * Checking to see if we can recover
 */
        if (info->bank_status & IA32_MCI_STATUS_PCC) {
            info->severity_string = "FATAL (UC+PCC)";
            return MC_SEVERITY_FATAL;
        }
        
        info->severity_string = "RECOVERABLE (UC)";
        return MC_SEVERITY_RECOVERABLE;
    }
    
    /*
 * Corrected error
 */
    info->severity_string = "CORRECTED";
    return MC_SEVERITY_CORRECTED;
}

/*
 * Machine Check information dump
 */
/*
 * Machine Check information dump
 */
static void dump_mc_info(mc_info_t *info, exception_frame_t *frame) {
    u64 cr2, cr3;
    
    /*
 * Reading CR2 and CR3 directly from the CPU
 */
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    
    terminal_error_printf("\n");
    terminal_error_printf("========================================\n");
    terminal_error_printf("      MACHINE CHECK EXCEPTION\n");
    terminal_error_printf("========================================\n");
    terminal_error_printf("Severity: %s\n", info->severity_string);
    terminal_error_printf("\n--- MCG Registers ---\n");
    terminal_error_printf("MCG_CAP:    0x%llx (banks: %d)\n", info->mcg_cap, info->num_banks);
    terminal_error_printf("MCG_STATUS: 0x%llx\n", info->mcg_status);
    terminal_error_printf("  PCC:  %d (Processor Context Corrupt)\n", info->pcc);
    terminal_error_printf("  RIPV: %d (RIP valid)\n", info->ripv);
    terminal_error_printf("  EIPV: %d (EIP valid)\n", info->eipv);
    terminal_error_printf("  MCIP: %d (MC in progress)\n", info->mcip);
    
    if (info->bank_status) {
        terminal_error_printf("\n--- First Failed Bank ---\n");
        terminal_error_printf("Status: 0x%llx\n", info->bank_status);
        terminal_error_printf("  UC:    %d (Uncorrected)\n", !!(info->bank_status & IA32_MCI_STATUS_UC));
        terminal_error_printf("  PCC:   %d (Context Corrupt)\n", !!(info->bank_status & IA32_MCI_STATUS_PCC));
        terminal_error_printf("  ADDRV: %d (Address valid)\n", !!(info->bank_status & IA32_MCI_STATUS_ADDRV));
        terminal_error_printf("Address: 0x%llx\n", info->bank_addr);
        if (info->bank_status & IA32_MCI_STATUS_MISCV) {
            terminal_error_printf("Misc:    0x%llx\n", info->bank_misc);
        }
    }
    
    terminal_error_printf("\n--- Faulting Context ---\n");
    terminal_error_printf("RIP: 0x%llx\n", frame->rip);
    terminal_error_printf("CS:  0x%llx\n", frame->cs);
    terminal_error_printf("RSP: 0x%llx\n", frame->rsp);
    terminal_error_printf("RFLAGS: 0x%llx\n", frame->rflags);
    terminal_error_printf("CR2: 0x%llx\n", cr2);
    terminal_error_printf("CR3: 0x%llx\n", cr3);
    terminal_error_printf("========================================\n");
}

/*
 * Main handler
 */
void handle_machine_check(mc_info_t *info, exception_frame_t *frame) {
    /*
 * Define the context (kernel or user)
 */
    info->kernel_context = ((frame->cs & 3) == 0);
    
    /*
 * Reading MCA registers
 */
    read_mca_registers(info);
    
    /*
 * Determining the severity
 */
    info->severity = determine_mc_severity(info);
    
    /*
 * Dump information
 */
    dump_mc_info(info, frame);
    
    switch (info->severity) {
        case MC_SEVERITY_CORRECTED:
            terminal_warn_printf("[#MC] Corrected error - ignoring\n");
            /*
 * Reset status
 */
            wrmsr(MSR_IA32_MCG_STATUS, 0);
            break;
            
        case MC_SEVERITY_RECOVERABLE:
            terminal_error_printf("[#MC] Recoverable error\n");
            
            if (!info->kernel_context && info->ripv) {
                /*
 * Error in the user process - kill it
 */
                terminal_error_printf("[#MC] Killing current process\n");
                task_exit(-1);
            } else {
                /*
 * Error in the kernel or RIP is not valid
 */
                panic("MACHINE_CHECK_RECOVERABLE_FAILED");
            }
            break;
            
        case MC_SEVERITY_FATAL:
        default:
            /*
 * Fatal error - immediately panic
 */
            panic("MACHINE_CHECK_FATAL");
            break;
    }
}
