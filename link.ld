OUTPUT_FORMAT(elf64-x86-64)
ENTRY(start)

PHDRS
{
  text PT_LOAD FLAGS(5); /* PF_R | PF_X */
  data PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS
{
    /* Физический адрес (нижняя половина) */
    . = 0x00100000;
    _kernel_phys_start = .;
    
    /* Адрес в higher half, куда скопируем ядро */
    _kernel_virt_start = 0xFFFFFFFF80000000 + _kernel_phys_start;
    
    .multiboot ALIGN(8) : {
        *(.multiboot)
    }
    
    .text : {
        *(.text)
        *(.text*)
    }
    
    .rodata : {
        *(.rodata)
        *(.rodata*)
    }
    
    .data : {
        *(.data)
        *(.data*)
    }
    
    .bss : {
        *(.bss)
        *(.bss*)
        *(COMMON)
        
        . = ALIGN(16);
        _stack_start = .;
        . += 262144;
        _stack_end = .;

        . = ALIGN(16);
        _tss_buffer = .;
        . += 104;          /* TSS */
    }

    .heap (NOLOAD) : {
        . = ALIGN(8);
        _heap_start = .;
        . = . + 8 * 1024 * 1024;  /* 8 MB кучи */
        _heap_end = .;
    } :data
    
    _kernel_phys_end = .;
    
    /* Символы для копирования */
    _kernel_size = _kernel_phys_end - _kernel_phys_start;
}
