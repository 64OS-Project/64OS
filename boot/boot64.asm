[BITS 64]

extern _stack_end
extern kmain
global long_mode_entry

long_mode_entry:
    ; Segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    
    ; Stack
    lea rsp, [rel _stack_end]
    and rsp, -16
    
    ; Kernel call (rbx = multiboot info)
    mov rdi, rbx
    call kmain

.hang64:
    hlt
    jmp .hang64

section .note.GNU-stack