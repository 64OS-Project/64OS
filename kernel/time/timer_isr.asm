; timer_isr.asm
[BITS 64]

global timer_isr
extern timer_apic_handler
extern schedule_from_isr
extern need_reschedule

timer_isr:
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
    
    push qword 0        ; err_code
    push qword 32       ; int_no

    call timer_apic_handler

;--- Check need_reschedule ---
    cmp byte [rel need_reschedule], 0
    je .no_schedule

    sub rsp, 8
    lea rdi, [rsp + 8]      ; frame pointer
    mov rsi, rsp            ; &out_slot
    call schedule_from_isr
    mov rsp, [rsp]          ;switch stack

.no_schedule:
    ;remove int_no and err_code
    add rsp, 16

    ;restoring registers
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

    iretq

section .note.GNU-stack
