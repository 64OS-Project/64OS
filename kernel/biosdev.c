#include <kernel/biosdev.h>
#include <kernel/terminal.h>
#include <libk/string.h>

void biosdev_parse(u8 bios_number, boot_device_info_t *info) {
    if (!info) return;
    
    memset(info, 0, sizeof(boot_device_info_t));
    info->bios_number = bios_number;
    info->is_valid = true;
    
    //Analyzing the bits
    if (bios_number >= 0x00 && bios_number <= 0x01) {
        //Floppy
        info->type = BOOT_DEVICE_FLOPPY;
        info->device_number = bios_number;
        info->is_removable = true;
        snprintf(info->name, sizeof(info->name), "floppy%d", bios_number);
    }
    else if (bios_number >= 0x80 && bios_number <= 0x8F) {
        //Hard drive
        info->type = BOOT_DEVICE_HDD;
        info->device_number = bios_number - 0x80;
        info->is_removable = false;
        snprintf(info->name, sizeof(info->name), "hdd%d", info->device_number);
    }
    else if (bios_number >= 0xE0 && bios_number <= 0xEF) {
        // CD-ROM
        info->type = BOOT_DEVICE_CDROM;
        info->device_number = bios_number - 0xE0;
        info->is_removable = true;
        snprintf(info->name, sizeof(info->name), "cdrom%d", info->device_number);
    }
    else if (bios_number >= 0xF0 && bios_number <= 0xFF) {
        //Other removable (flash drives, ZIP, etc.)
        info->type = BOOT_DEVICE_REMOVABLE;
        info->device_number = bios_number - 0xF0;
        info->is_removable = true;
        snprintf(info->name, sizeof(info->name), "removable%d", info->device_number);
    }
    else {
        //Unknown type
        info->type = BOOT_DEVICE_UNKNOWN;
        info->device_number = bios_number;
        info->is_removable = false;
        snprintf(info->name, sizeof(info->name), "unknown_0x%x", bios_number);
    }
}

const char* biosdev_type_to_string(boot_device_type_t type) {
    switch (type) {
        case BOOT_DEVICE_FLOPPY:    return "Floppy";
        case BOOT_DEVICE_HDD:       return "Hard Disk";
        case BOOT_DEVICE_CDROM:     return "CD-ROM";
        case BOOT_DEVICE_NETWORK:   return "Network";
        case BOOT_DEVICE_REMOVABLE: return "Removable";
        default:                    return "Unknown";
    }
}

void biosdev_print_info(boot_device_info_t *info) {
    if (!info || !info->is_valid) {
        terminal_printf("Boot device: invalid\n");
        return;
    }
    
    terminal_printf("\n=== Boot Device Info ===\n");
    terminal_printf("BIOS number: 0x%02x\n", info->bios_number);
    terminal_printf("Type: %s\n", biosdev_type_to_string(info->type));
    terminal_printf("Device number: %d\n", info->device_number);
    terminal_printf("Removable: %s\n", info->is_removable ? "Yes" : "No");
    terminal_printf("Name: %s\n", info->name);
    
    //Additional information
    if (info->type == BOOT_DEVICE_CDROM) {
        terminal_printf("Note: Booted from CD-ROM (Live CD)\n");
        terminal_printf("      You can install system to hard disk\n");
    } else if (info->type == BOOT_DEVICE_HDD) {
        terminal_printf("Note: Booted from hard disk\n");
        terminal_printf("      System is installed\n");
    }
    terminal_printf("========================\n");
}
