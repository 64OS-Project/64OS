#include <kernel/df.h>
#include <kernel/panic.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <asm/cpu.h>

/*
 * Analysis of the cause of double error
 */
df_diagnostic_t analyze_double_fault(df_frame_t *frame, u64 original_error_code) {
    df_diagnostic_t diag;
    memset(&diag, 0, sizeof(diag));
    
    diag.original_exception = original_error_code & 0xFF;
    diag.nested_exception = (original_error_code >> 8) & 0xFF;
    
    /*
 * Determining the cause
 */
    if (frame->rsp < 0xFFFFFFFF80000000ULL) {
        diag.cause = DF_CAUSE_STACK_OVERFLOW;
        diag.cause_string = "STACK_OVERFLOW";
        
        /*
 * Trying to find the last valid RSP (rough heuristics)
 */
        u64 *ptr = (u64*)(frame->rsp - 8);
        for (int i = 0; i < 16; i++) {
            if ((u64)ptr > 0xFFFFFFFF80000000ULL && (u64)ptr < 0xFFFFFFFFFFFFFFFFULL) {
                diag.last_valid_rsp = (u64)ptr;
                break;
            }
            ptr--;
        }
    } 
    else if (frame->cr2 != 0) {
        diag.cause = DF_CAUSE_PAGING_ERROR;
        diag.cause_string = "PAGING_ERROR";
    }
    else if (original_error_code & 0x10000) {
        diag.cause = DF_CAUSE_GDT_IDT_ERROR;
        diag.cause_string = "GDT_IDT_ERROR";
    }
    else {
        diag.cause = DF_CAUSE_UNKNOWN;
        diag.cause_string = "UNKNOWN";
    }
    
    /*
 * Checking for stack corruption
 */
    u64 *stack = (u64*)frame->rsp;
    diag.stack_corrupted = (stack[0] == 0xDEADBEEFDEADBEEFULL || 
                             stack[0] == 0xFFFFFFFFFFFFFFFFULL ||
                             stack[0] == 0);
    
    return diag;
}

/*
 * Double error dump
 */
void dump_double_fault_info(df_diagnostic_t *diag, df_frame_t *frame) {
    terminal_error_printf("\n");
    terminal_error_printf("========================================\n");
    terminal_error_printf("     DOUBLE FAULT DIAGNOSTICS\n");
    terminal_error_printf("========================================\n");
    terminal_error_printf("Cause: %s\n", diag->cause_string);
    terminal_error_printf("Original exception: #%d\n", (int)diag->original_exception);
    terminal_error_printf("Nested exception:   #%d\n", (int)diag->nested_exception);
    terminal_error_printf("Stack corrupted:    %s\n", diag->stack_corrupted ? "YES" : "NO");
    
    if (diag->last_valid_rsp) {
        terminal_error_printf("Last valid RSP:     0x%llx\n", diag->last_valid_rsp);
    }
    
    terminal_error_printf("\n--- CPU State at Fault ---\n");
    terminal_error_printf("RIP: 0x%llx\n", frame->rip);
    terminal_error_printf("RSP: 0x%llx\n", frame->rsp);
    terminal_error_printf("RBP: 0x%llx\n", frame->rbp);
    terminal_error_printf("CR2: 0x%llx\n", frame->cr2);
    terminal_error_printf("CR3: 0x%llx\n", frame->cr3);
    terminal_error_printf("CS:  0x%llx\n", frame->cs);
    terminal_error_printf("RFLAGS: 0x%llx\n", frame->rflags);
    
    /*
 * Stack dump if not completely killed
 */
    if (!diag->stack_corrupted && frame->rsp > 0xFFFFFFFF80000000ULL) {
        terminal_error_printf("\n--- Stack Dump (RSP + 32 bytes) ---\n");
        u64 *stack = (u64*)frame->rsp;
        for (int i = 0; i < 8; i++) {
            terminal_error_printf("[RSP+%d] 0x%llx\n", i * 8, stack[i]);
        }
    }
    
    terminal_error_printf("========================================\n");
}

/*
 * Main handler (called from asm)
 */
void handle_double_fault(df_frame_t *frame, u64 original_error_code) {
    df_diagnostic_t diag = analyze_double_fault(frame, original_error_code);
    dump_double_fault_info(&diag, frame);
    
    /*
 * Unconditional panic
 */
    panic("DOUBLE_FAULT");
}
