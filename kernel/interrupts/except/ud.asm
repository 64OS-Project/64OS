[BITS 64]

global ud
extern handle_invalid_opcode
extern panic

section .text

ud:
    ;We save all registers (as in your style)
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
    
    ;Determining the context according to CS
    mov rcx, [rsp + 120]      ;CS (after pushes)
    and rcx, 3                 ;We take DPL
    cmp rcx, 3
    je .user_mode
    
.kernel_mode:
    mov rsi, 0                 ; UD_CONTEXT_KERNEL
    jmp .call_handler
    
.user_mode:
    mov rsi, 1                 ; UD_CONTEXT_USER
    
.call_handler:
    mov rdi, rsp               ; exception_frame_t*
    call handle_invalid_opcode
    
    ;Checking if the exception has been handled
    cmp byte [rax + 0], 0      ; result.handled
    jne .handled
    
    ;Not handled -> panic for the kernel
    mov rdi, invalid_opcode_msg
    call panic
    
.handled:
    ;Restoring registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    
    ;Remove error_code (0) and int_no (6)
    add rsp, 16
    
    iretq

section .rodata
invalid_opcode_msg:
    db "KERNEL_INVALID_OPCODE", 0

section .note.GNU-stack
