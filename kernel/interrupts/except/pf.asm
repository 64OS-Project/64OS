[BITS 64]

global pf
extern handle_page_fault

pf:
    ;save registers
    push rax
    push rbx
    push rcx
    push rdx
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
    ;15 × 8 = 120 bytes, CPU data offsets are the same:
    ;   rsp+120 = error_code
    ;   rsp+128 = rip
    ;   rsp+136 = cs

    mov  rdi, cr2           ;arg1: violation address (CR2 stores it for #PF)
    mov  rsi, [rsp + 120]   ; arg2: error_code
    mov  rdx, [rsp + 128]   ; arg3: rip
    mov  rcx, [rsp + 136]   ; arg4: cs

    sub  rsp, 8
    call handle_page_fault
    add  rsp, 8

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

    add rsp, 8   ;remove the error_code that the CPU put in itself
    iretq

section .note.GNU-stack
; empty
