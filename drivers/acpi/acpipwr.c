#include <acpi.h>
#include <asm/io.h>
#include <asm/cpu.h>

extern acpi_t g_acpi;

void acpi_reboot(void) {
    if (!g_acpi.fadt) {
        outb(0x64, 0xFE);
        return;
    }
    
    u8 *reset_reg = g_acpi.fadt->reset_reg;
    
    if (reset_reg[0] == 0x01) {
        u16 port = *(u16*)(reset_reg + 4);
        u8 value = g_acpi.fadt->reset_value;
        outb(port, value);
    } else if (reset_reg[0] == 0x02) {
        u64 addr = *(u64*)(reset_reg + 4);
        u8 value = g_acpi.fadt->reset_value;
        *(volatile u8*)(uptr)addr = value;
    } else {
        outb(0x64, 0xFE);
    }
    
    while(1) {
        halt();
    }
}

void acpi_shutdown(void) {
    if (!g_acpi.fadt) {
        return;
    }
    
    u16 pm1a_port = g_acpi.fadt->pm1a_cnt_blk;
    if (!pm1a_port) {
        return;
    }
    
    u16 value = (5 << 10) | (1 << 13);
    outw(pm1a_port, value);
    
    u16 pm1b_port = g_acpi.fadt->pm1b_cnt_blk;
    if (pm1b_port) {
        outw(pm1b_port, value);
    }
    
    while(1) {
        halt();
    }
}

void acpi_sleep(u8 sleep_type) {
    if (!g_acpi.fadt) return;
    
    u16 pm1a_port = g_acpi.fadt->pm1a_cnt_blk;
    if (!pm1a_port) return;
    
    u16 value = (sleep_type << 10) | (1 << 13);
    outw(pm1a_port, value);
    
    u16 pm1b_port = g_acpi.fadt->pm1b_cnt_blk;
    if (pm1b_port) {
        outw(pm1b_port, value);
    }
}