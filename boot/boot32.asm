[BITS 32]

section .text
    align 8

; --------------------
; Multiboot2 header
; --------------------
align 8
mb2_header_start:
    dd 0xE85250D6
    dd 0x00000000
    dd mb2_header_end - mb2_header_start
    dd -(0xE85250D6 + 0x00000000 + (mb2_header_end - mb2_header_start))

align 8
info_request_tag:
    dw 1
    dw 0
    dd info_request_tag_end - info_request_tag
    dd 6
info_request_tag_end:

; Framebuffer tag
align 8
fb_tag_start:
    dw 5
    dw 0
    dw fb_tag_end - fb_tag_start
    dd 0
    dd 0
    dd 32
fb_tag_end:

; End tag
align 8
end_tag:
    dw 0
    dw 0
    dd 8
mb2_header_end:

global start
global gdt
global tss_buffer
global stack64_top
extern long_mode_entry

; Constants
PAGE_SIZE equ 4096
PDE_2MB_FLAGS equ 0x083  ; Present, Writable, 2MB page, Global

start:
    cli
    
    ; Loading GDT
    lgdt [gdt_desc]
    
    ; Enabling PAE
    mov eax, cr4
    bts eax, 5
    mov cr4, eax

    mov eax, cr0
    and eax, 0xFFFFFFFB  ; Clear CR0.EM
    or eax, 0x2          ; Set CR0.MP
    mov cr0, eax

    mov eax, cr4
    or eax, 0x600        ; Set CR4.OSFXSR and CR4.OSXMMEXCPT
    mov cr4, eax
    
    fninit               ; Initialize FPU/SSE
    
    ; Setting CR3 (PML4 addr)
    mov eax, pml4_table
    mov cr3, eax
    
    ; Enabling Long Mode via MSR
    mov ecx, 0xC0000080
    rdmsr
    bts eax, 8
    wrmsr
    
    ; Enabling paging
    mov eax, cr0
    bts eax, 31
    mov cr0, eax
    
    ; Jump to long mode
    jmp 0x08:long_mode_entry

; -----------------------------------------------------------------------
; GDT
; -----------------------------------------------------------------------
align 8
gdt:
    dq 0x0000000000000000     ; Null
    dq 0x00AF9A000000FFFF     ; 0x08: Kernel Code
    dq 0x00AF92000000FFFF     ; 0x10: Kernel Data
    dq 0x00AFFA000000FFFF     ; 0x18: User Code
    dq 0x00AFF2000000FFFF     ; 0x20: User Data
    dq 0x0000000000000000     ; 0x28: TSS lower
    dq 0x0000000000000000     ; 0x30: TSS upper
gdt_end:

global gdt_desc
gdt_desc:
    dw gdt_end - gdt - 1
    dq gdt

; -----------------------------------------------------------------------
; Page Tables - Identity map
; -----------------------------------------------------------------------
section .data
align 4096

global pml4_table
global kernel_pml4 

; PML4 - 2 entries for 1TB (512GB on entry)
pml4_table:
kernel_pml4:
    dq pdp_table0 + 0x007    ; 0-512GB
    dq pdp_table1 + 0x007    ; 512GB-1TB
    times 510 dq 0

; PDPT #0 - first 512GB
align 4096
pdp_table0:
    %assign i 0
    %rep 512
        dq (pd_tables0 + i * PAGE_SIZE) + 0x007
        %assign i i+1
    %endrep

; PDPT #1 - secong 512GB (1TB all)
align 4096
pdp_table1:
    %assign i 0
    %rep 512
        dq (pd_tables1 + i * PAGE_SIZE) + 0x007
        %assign i i+1
    %endrep

; PD table for first 512GB (512 tables × 1GB = 512GB)
align 4096
pd_tables0:
    %assign table_idx 0
    %rep 512
        %assign phys_addr table_idx * 0x40000000  ; 1GB on table
        %assign entry_idx 0
        %rep 512
            dq (phys_addr + entry_idx * 0x200000) + PDE_2MB_FLAGS
            %assign entry_idx entry_idx+1
        %endrep
        %assign table_idx table_idx+1
    %endrep

; PD tables for second 512GB (512 tables × 1GB = 512GB)
align 4096
pd_tables1:
    %assign table_idx 0
    %rep 512
        %assign phys_addr 0x2000000000 + (table_idx * 0x40000000)  ; 128GB + 1GB*table
        %assign entry_idx 0
        %rep 512
            dq (phys_addr + entry_idx * 0x200000) + PDE_2MB_FLAGS
            %assign entry_idx entry_idx+1
        %endrep
        %assign table_idx table_idx+1
    %endrep

section .note.GNU-stack
