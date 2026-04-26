[BITS 64]

global mc
extern handle_machine_check

section .text

mc:
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
    
    ;Allocate space on the stack for mc_info_t (192 bytes is enough)
    sub rsp, 192
    
    ;Fill mc_info_t with zeros
    mov rdi, rsp
    mov rcx, 24               ; 192 / 8 = 24 qwords
    xor rax, rax
    rep stosq
    
    ;Restoring rdi for a call
    mov rdi, rsp              ; mc_info_t*
    mov rsi, rbp              ;exception_frame_t* (RBP points to the stack after saving)
    ;Actually frame = RSP + 192 (after allocation) + 120 (registers)?
    ;It is easier to transmit the current RSP + 192 + 120 - 8
    
    ;Correct calculation: frame = RSP current + 192 (mc_info) + 120 (registers) + 8 (alignment?)
    lea rsi, [rsp + 192 + 120]
    call handle_machine_check
    
    ;Freeing mc_info_t
    add rsp, 192
    
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
    
    ;Remove error_code (0) and int_no (18)
    add rsp, 16
    
    iretq

section .note.GNU-stack
