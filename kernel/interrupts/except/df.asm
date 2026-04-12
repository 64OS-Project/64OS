[BITS 64]

global df
extern handle_double_fault

section .text

df:
    ;#DF does not have an error_code from the CPU, but the processor puts 0
    ;Saving registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ;After saving the registers:
    ;RSP indicates last push (r15)
    ;error_code is located at RSP + 120 (15 pushes * 8 = 120)
    ;int_no is located at address RSP + 128 (120 + 8)
    
    mov rdi, rsp               ;df_frame_t* (pointer to the beginning of the saved registers)
    mov rsi, [rsp + 120]       ; original_error_code
    call handle_double_fault
    
    ;We'll never get here (panic inside handle_double_fault)
    jmp $

section .note.GNU-stack
