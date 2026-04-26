#ifndef EXCEPTIONS_MC_H
#define EXCEPTIONS_MC_H

#include <kernel/types.h>
#include <kernel/ud.h>

/*
 * Machine Check MSRs
 */
#define MSR_IA32_MCG_CAP         0x179
#define MSR_IA32_MCG_STATUS      0x17A
#define MSR_IA32_MCG_CTL         0x17B
#define MSR_IA32_MCG_EXT_CTL     0x17D

/*
 * Global status bits
 */
#define MCG_STATUS_RIPV          (1ULL << 0)
#define MCG_STATUS_EIPV          (1ULL << 1)
#define MCG_STATUS_MCIP          (1ULL << 2)
#define MCG_STATUS_PCC           (1ULL << 3)  /*
 * Processor Context Corrupt
 */

/*
 * Error severity
 */
typedef enum {
    MC_SEVERITY_CORRECTED,      /*
 * Bug fixed (can be ignored)
 */
    MC_SEVERITY_RECOVERABLE,    /*
 * Recoverable (only kill the process)
 */
    MC_SEVERITY_FATAL,          /*
 * Fatal (kernel panic)
 */
    MC_SEVERITY_UNKNOWN
} mc_severity_t;

/*
 * Error information
 */
typedef struct {
    bool pcc;                    /*
 * Processor Context Corrupt
 */
    bool ripv;                   /*
 * RIP valid?
 */
    bool eipv;                   /*
 * EIP valid?
 */
    bool mcip;                   /*
 * Machine check in progress
 */
    
    u64 mcg_cap;                 /*
 * MCG_CAP MSR
 */
    u64 mcg_status;              /*
 * MCG_STATUS MSR
 */
    
    u32 num_banks;               /*
 * Number of banks
 */
    u64 bank_status;             /*
 * Status of the first failed bank
 */
    u64 bank_addr;               /*
 * Error address
 */
    u64 bank_misc;               /*
 * Misc info
 */
    
    mc_severity_t severity;
    const char *severity_string;
    
    bool kernel_context;         /*
 * Kernel bug?
 */
} mc_info_t;

/*
 * Handler #MC
 */
void handle_machine_check(mc_info_t *info, exception_frame_t *frame);

/*
 * Reading MCA Registers
 */
void read_mca_registers(mc_info_t *info);

/*
 * Definition of severity
 */
mc_severity_t determine_mc_severity(mc_info_t *info);

#endif
