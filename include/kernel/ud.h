#ifndef EXCEPTIONS_UD_H
#define EXCEPTIONS_UD_H

#include <kernel/types.h>

typedef struct {
    /*
 * Saved registers (in push order)
 */
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rdi;
    u64 rsi;
    u64 rbp;
    u64 rbx;
    u64 rdx;
    u64 rcx;
    u64 rax;
    
    /*
 * Puts CPU down (for exceptions with error_code)
 */
    u64 error_code;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
} exception_frame_t;

typedef enum {
    UD_CONTEXT_KERNEL,
    UD_CONTEXT_USER
} ud_context_t;

typedef struct {
    bool handled;
    const char *reason;
} ud_result_t;

/*
 * #UD handler
 */
ud_result_t handle_invalid_opcode(exception_frame_t *frame, ud_context_t context);

/*
 * Instruction emulation (if needed)
 */
bool emulate_instruction(exception_frame_t *frame);

/*
 * RIP decoding (for debugging)
 */
void dump_invalid_instruction(u64 rip);

#endif
