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

    ;IA32_EFER (MSR 0xC0000080) - enable SCE (System Call Extensions)
    mov ecx, 0xC0000080
    rdmsr
    bts eax, 0              ; SCE bit
    wrmsr

    mov ecx, 0xC0000081
    mov eax, 0x001B000800000023   ;Lower 32 bits: User SS = 0x23
    mov edx, 0x001B0008           ;Upper 32 bits: Kernel CS = 0x08, User CS = 0x1B
    wrmsr

    mov ecx, 0xC0000082
    mov rax, syscall_entry
    mov rdx, rax
    shr rdx, 32
    wrmsr

    mov ecx, 0xC0000084
    mov eax, 0x00000000    ;Lower 32 bits of the mask
    mov edx, 0x00000000    ;Upper 32 bits (clear everything except IF? need some thought)
    ;Better: clear IF, DF, AC, etc.
    mov eax, 0x00000200     ;Bit 9 = IF (interrupts)
    mov edx, 0x00000000
    wrmsr
    
    ; Kernel call (rbx = multiboot info)
    mov rdi, rbx
    call kmain

.hang64:
    hlt
    jmp .hang64

extern syscall_handler
syscall_entry:
    ;Saving the user context
    swapgs                  ;Changing GS to kernel GS
    mov [gs:0x00], rsp      ;Save user RSP (if necessary)
    
    ;Switching to the kernel stack
    mov rsp, [gs:0x08]      ;Loading kernel RSP from GS
    
    ;Saving registers
    push rcx                ;user RIP (return)
    push r11                ; user RFLAGS
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    ;First 6 arguments in rdi, rsi, rdx, r10, r8, r9
    ;(r10 is used instead of rcx, because rcx is overwritten)
    mov rdi, rax            ;syscall number
    mov rsi, rdi            ;arg1 (is the original rdi preserved? need to be careful)
    ;In fact: when syscall:
    ;rax = system call number
    ; rdi = arg1
    ; rsi = arg2  
    ; rdx = arg3
    ;r10 = arg4 (rcx is overwritten)
    ; r8 = arg5
    ; r9 = arg6
    
    mov rcx, r10            ;move arg4 to rcx for C ABI
    
    call syscall_handler
    
    ;Restoring registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop r11                ; user RFLAGS
    pop rcx                ; user RIP
    
    ; Exit via sysret
    swapgs                  ;back to user GS
    sysretq

section .note.GNU-stack
