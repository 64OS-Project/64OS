#include <kernel/types.h>
#include <kernel/terminal.h>
#include <kernel/blockdev.h>
#include <libk/string.h>

void format_size(u64 bytes, char *buffer, u32 buf_size) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size_d = (double)bytes;
    
    while (size_d >= 1024.0 && unit < 4) {
        size_d /= 1024.0;
        unit++;
    }
    
    snprintf(buffer, buf_size, "%.2f %s", size_d, units[unit]);
}

void cmd_disklist(void) {
    blockdev_t* list[MAX_BLOCK_DEVS];
    int count = blockdev_get_list(list, MAX_BLOCK_DEVS);
    
    if (count == 0) {
        terminal_printf("No block devices found\n");
        return;
    }
    
    terminal_printf("\n=== Block Devices (%d total) ===\n", count);
    terminal_printf("%-4s %-12s %-8s %-10s %-8s %s\n", 
                   "No.", "Name", "Type", "Size", "Status", "Sectors");
    terminal_printf("---- ------------ -------- ---------- -------- -----------------\n");
    
    for (int i = 0; i < count; i++) {
        char size_str[32];
        format_size(list[i]->total_bytes, size_str, sizeof(size_str));
        
        const char* type_str = "Unknown";
        switch (list[i]->type) {
            case BLOCKDEV_TYPE_IDE: type_str = "IDE"; break;
            case BLOCKDEV_TYPE_AHCI: type_str = "AHCI"; break;
            case BLOCKDEV_TYPE_RAMDISK: type_str = "RAM"; break;
        }
        
        const char* status_str = "Unknown";
        switch (list[i]->status) {
            case BLOCKDEV_READY: status_str = "Ready"; break;
            case BLOCKDEV_ERROR: status_str = "Error"; break;
            case BLOCKDEV_NO_MEDIA: status_str = "No Media"; break;
            case BLOCKDEV_UNINITIALIZED: status_str = "Uninit"; break;
        }
        
        terminal_printf("%-4d %-12s %-8s %-10s %-8s %lu\n",
                       i + 1,
                       list[i]->name,
                       type_str,
                       size_str,
                       status_str,
                       (unsigned long)list[i]->total_sectors);
    }
    terminal_printf("\n");
}

void cmd_diskinfo(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: diskinfo <disk_name>\n");
        terminal_printf("Example: diskinfo sata_0\n");
        terminal_printf("Example: diskinfo ide_1\n");
        return;
    }
    
    //Remove spaces at the beginning
    while (*args == ' ') args++;
    
    blockdev_t* dev = blockdev_find(args);
    if (!dev) {
        terminal_error_printf("Device '%s' not found!\n", args);
        
        blockdev_t* list[MAX_BLOCK_DEVS];
        int count = blockdev_get_list(list, MAX_BLOCK_DEVS);
        
        if (count > 0) {
            terminal_printf("Available devices: ");
            for (int i = 0; i < count; i++) {
                terminal_printf("%s", list[i]->name);
                if (i < count - 1) terminal_printf(", ");
            }
            terminal_printf("\n");
        }
        return;
    }
    
    char size_str[32];
    format_size(dev->total_bytes, size_str, sizeof(size_str));
    
    const char* type_str = "Unknown";
    switch (dev->type) {
        case BLOCKDEV_TYPE_IDE: type_str = "IDE"; break;
        case BLOCKDEV_TYPE_AHCI: type_str = "AHCI"; break;
        case BLOCKDEV_TYPE_RAMDISK: type_str = "RAM Disk"; break;
    }
    
    const char* status_str = "Unknown";
    switch (dev->status) {
        case BLOCKDEV_READY: status_str = "Ready"; break;
        case BLOCKDEV_ERROR: status_str = "Error"; break;
        case BLOCKDEV_NO_MEDIA: status_str = "No Media"; break;
        case BLOCKDEV_UNINITIALIZED: status_str = "Uninitialized"; break;
    }
    
    terminal_printf("\n========================================\n");
    terminal_printf("  Device: %s\n", dev->name);
    terminal_printf("========================================\n");
    terminal_printf("  Type:           %s\n", type_str);
    terminal_printf("  Status:         %s\n", status_str);
    terminal_printf("  Size:           %s (%lu bytes)\n", size_str, (unsigned long)dev->total_bytes);
    terminal_printf("  Sector size:    %lu bytes\n", (unsigned long)dev->sector_size);
    terminal_printf("  Total sectors:  %lu\n", (unsigned long)dev->total_sectors);
    terminal_printf("  LBA48 support:  %s\n", dev->supports_lba48 ? "Yes" : "No");
    terminal_printf("  Read handler:   %s\n", dev->read_sectors ? "Present" : "MISSING!");
    terminal_printf("  Write handler:  %s\n", dev->write_sectors ? "Present" : "MISSING!");
    terminal_printf("  Stats:\n");
    terminal_printf("    Reads:  %lu\n", (unsigned long)dev->read_count);
    terminal_printf("    Writes: %lu\n", (unsigned long)dev->write_count);
    terminal_printf("    Errors: %lu\n", (unsigned long)dev->error_count);
    
    if (dev->type == BLOCKDEV_TYPE_IDE) {
        terminal_printf("  IDE Info:\n");
        terminal_printf("    Channel: %s\n", dev->device_data.ide.channel == 0 ? "Primary" : "Secondary");
        terminal_printf("    Drive:   %s\n", dev->device_data.ide.drive == 0 ? "Master" : "Slave");
    }
    
    if (dev->type == BLOCKDEV_TYPE_AHCI) {
        terminal_printf("  AHCI Info:\n");
        terminal_printf("    Port:    %d\n", dev->device_data.ahci.port_num);
    }
    
    terminal_printf("========================================\n\n");
}
