#ifndef EXCEPTIONS_DF_H
#define EXCEPTIONS_DF_H

#include <kernel/types.h>
#include <kernel/ud.h>

typedef struct {
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 error_code;
    u64 cr2;
    u64 cr3;
    u64 rsp;
    u64 rbp;
} df_frame_t;

typedef enum {
    DF_CAUSE_STACK_OVERFLOW,
    DF_CAUSE_PAGING_ERROR,
    DF_CAUSE_GDT_IDT_ERROR,
    DF_CAUSE_UNKNOWN
} df_cause_t;

/*
 * Double error diagnostic structure
 */
typedef struct {
    df_cause_t cause;
    const char *cause_string;
    u64 original_exception;
    u64 nested_exception;
    bool stack_corrupted;
    u64 last_valid_rsp;
} df_diagnostic_t;

/*
 * #DF handler (always panic)
 */
void handle_double_fault(df_frame_t *frame, u64 original_error_code);

/*
 * Diagnosis of the cause
 */
df_diagnostic_t analyze_double_fault(df_frame_t *frame, u64 original_error_code);

/*
 * State dump
 */
void dump_double_fault_info(df_diagnostic_t *diag, df_frame_t *frame);

#endif
