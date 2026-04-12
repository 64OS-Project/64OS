#include <kernel/blockdev.h>
#include <kdisk/ide.h>
#include <kdisk/ahci.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <stdarg.h>

/*
 * Global Variables
 */
static blockdev_t *blockdev_list_head = NULL; /*
 * Name changed!
 */
static int blockdev_count = 0;
extern ide_disk_t disks[4];

/*
 * Formatting Helpers
 */
static void format_size(u64 bytes, char *buffer, sz size) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size_d = (double)bytes;
    
    while (size_d >= 1024.0 && unit < 4) {
        size_d /= 1024.0;
        unit++;
    }
    
    snprintf(buffer, size, "%.2f %s", size_d, units[unit]);
}

/*
 * Wrappers for IDE devices
 */
static int ide_read_wrapper(blockdev_t *bdev, u64 lba, 
                           u32 count, void *buffer) {
    if (!bdev || !buffer || count == 0) return -1;
    
    ide_disk_t *ide_disk = (ide_disk_t*)bdev->device_data.ide.ide_disk;
    if (!ide_disk) return -1;
    
    int result = ide_read_sectors(ide_disk, lba, count, buffer);
    if (result == IDE_OK) {
        bdev->read_count++;
        return 0;
    }
    
    bdev->error_count++;
    return -1;
}

static int ide_write_wrapper(blockdev_t *bdev, u64 lba, 
                            u32 count, const void *buffer) {
    if (!bdev || !buffer || count == 0) return -1;
    
    ide_disk_t *ide_disk = (ide_disk_t*)bdev->device_data.ide.ide_disk;
    if (!ide_disk) return -1;
    
    int result = ide_write_sectors(ide_disk, lba, count, buffer);
    if (result == IDE_OK) {
        bdev->write_count++;
        return 0;
    }
    
    bdev->error_count++;
    return -1;
}

static int ide_flush_wrapper(blockdev_t *bdev) {
    /*
 * IDE automatically flushes cache after every write
 */
    return 0;
}

static int ahci_read_wrapper(blockdev_t *bdev, u64 lba, u32 count, void *buffer) {
    ahci_port_t *port = (ahci_port_t*)bdev->device_data.ahci.ahci_port;
    if (!port) return -1;
    
    int result = ahci_port_read(port, lba, count, buffer);
    if (result == 0) {
        bdev->read_count++;
        return 0;
    }
    bdev->error_count++;
    return -1;
}

//Record wrapper
static int ahci_write_wrapper(blockdev_t *bdev, u64 lba, u32 count, const void *buffer) {
    ahci_port_t *port = (ahci_port_t*)bdev->device_data.ahci.ahci_port;
    if (!port) return -1;
    
    int result = ahci_port_write(port, lba, count, buffer);
    if (result == 0) {
        bdev->write_count++;
        return 0;
    }
    bdev->error_count++;
    return -1;
}

//AHCI port registration function
void blockdev_register_ahci(ahci_port_t *port, int port_num) {
    if (!port || port->status != AHCI_PORT_ACTIVE) return;
    
    char name[32];
    snprintf(name, sizeof(name), "sata_%d", port_num);
    
    blockdev_t *bdev = blockdev_register(name, BLOCKDEV_TYPE_AHCI);
    if (!bdev) return;
    
    bdev->sector_size = port->sector_size;
    bdev->total_sectors = port->total_sectors;
    bdev->total_bytes = port->total_sectors * port->sector_size;
    bdev->supports_lba48 = port->supports_lba48;
    bdev->status = BLOCKDEV_READY;
    
    bdev->read_sectors = ahci_read_wrapper;
    bdev->write_sectors = ahci_write_wrapper;
    bdev->flush_cache = NULL;
    
    bdev->device_data.ahci.ahci_port = port;
    bdev->device_data.ahci.port_num = port_num;
    
    terminal_printf("[BLOCKDEV] Registered AHCI port %d as %s (%lu MB)\n", 
               port_num, name, (unsigned long)(bdev->total_bytes / (1024 * 1024)));
}

/*
 * Initializing the block device system
 */
void blockdev_init(void) {
    terminal_printf("[BLOCKDEV] Initializing block device system...\n");
    
    blockdev_list_head = NULL;
    blockdev_count = 0;
    
    terminal_printf( "[BLOCKDEV] System ready (max %d devices)\n", 
                MAX_BLOCK_DEVS);
}

blockdev_t* blockdev_register(const char *name, blockdev_type_t type) {
    /*
 * Checks
 */
    if (!name || name[0] == '\0') {
        terminal_error_printf("[BLOCKDEV] Error: invalid device name\n");
        return NULL;
    }
    
    if (blockdev_count >= MAX_BLOCK_DEVS) {
        terminal_error_printf("[BLOCKDEV] Error: maximum devices reached\n");
        return NULL;
    }
    
    /*
 * Checking to see if there is already a device with the same name
 */
    if (blockdev_find(name)) {
        terminal_error_printf("[BLOCKDEV] Error: device '%s' already exists\n", 
                    name);
        return NULL;
    }
    
    /*
 * Allocating memory
 */
    blockdev_t *dev = (blockdev_t*)malloc(sizeof(blockdev_t));
    if (!dev) {
        terminal_error_printf("[BLOCKDEV] Error: memory allocation failed\n");
        return NULL;
    }
    
    /*
 * Initializing the structure
 */
    memset(dev, 0, sizeof(blockdev_t));
    
    /*
 * Copy the name
 */
    strncpy(dev->name, name, BLOCKDEV_NAME_LEN - 1);
    dev->name[BLOCKDEV_NAME_LEN - 1] = '\0';
    
    /*
 * Setting the type and status
 */
    dev->type = type;
    dev->status = BLOCKDEV_UNINITIALIZED;
    
    /*
 * Set default handlers (will be overridden)
 */
    dev->read_sectors = NULL;
    dev->write_sectors = NULL;
    dev->flush_cache = NULL;
    
    /*
 * Add to the list
 */
    dev->next = blockdev_list_head;
    blockdev_list_head = dev;
    blockdev_count++;
    
    terminal_printf("[BLOCKDEV] Registered device: %s (type: %d)\n", 
                name, type);
    
    return dev;
}

/*
 * Search for a device by name
 */
blockdev_t* blockdev_find(const char *name) {
    if (!name) return NULL;
    
    blockdev_t *dev = blockdev_list_head;
    while (dev) {
        if (strcmp(dev->name, name) == 0) {
            return dev;
        }
        dev = dev->next;
    }
    
    return NULL;
}

/*
 * Search for a device by number
 */
blockdev_t* blockdev_find_by_number(int num) {
    char name[BLOCKDEV_NAME_LEN];
    snprintf(name, sizeof(name), "dsk_%d", num);
    return blockdev_find(name);
}

/*
 * Getting a list of all devices
 */
int blockdev_get_list(blockdev_t **list, int max_count) {
    if (!list || max_count <= 0) return 0;
    
    int count = 0;
    blockdev_t *dev = blockdev_list_head;
    
    while (dev && count < max_count) {
        list[count++] = dev;
        dev = dev->next;
    }
    
    return count;
}

/*
 * Reading sectors
 */
int blockdev_read(blockdev_t *dev, u64 lba, 
                  u32 count, void *buffer) {
    /*
 * Checks
 */
    if (!dev || !buffer) {
        return -1;
    }
    
    if (dev->status != BLOCKDEV_READY) {
        terminal_error_printf("[BLOCKDEV] Device %s not ready for reading\n", 
                    dev->name);
        return -1;
    }
    
    if (lba + count > dev->total_sectors) {
        terminal_error_printf("[BLOCKDEV] Read out of bounds on %s\n", dev->name);
        return -1;
    }
    
    if (!dev->read_sectors) {
        terminal_error_printf("[BLOCKDEV] No read handler for %s\n", dev->name);
        return -1;
    }
    
    /*
 * Call the handler
 */
    return dev->read_sectors(dev, lba, count, buffer);
}

/*
 * Recording sectors
 */
int blockdev_write(blockdev_t *dev, u64 lba, 
                   u32 count, const void *buffer) {
    /*
 * Checks
 */
    if (!dev || !buffer) {
        return -1;
    }
    
    if (dev->status != BLOCKDEV_READY) {
        terminal_error_printf("[BLOCKDEV] Device %s not ready for writing\n", 
                    dev->name);
        return -1;
    }
    
    if (lba + count > dev->total_sectors) {
        terminal_error_printf("[BLOCKDEV] Write out of bounds on %s\n", dev->name);
        return -1;
    }
    
    if (!dev->write_sectors) {
        terminal_error_printf("[BLOCKDEV] No write handler for %s\n", dev->name);
        return -1;
    }
    
    /*
 * Call the handler
 */
    int result = dev->write_sectors(dev, lba, count, buffer);
    
    /*
 * If necessary, reset the cache
 */
    if (result == 0 && dev->flush_cache) {
        dev->flush_cache(dev);
    }
    
    return result;
}

/*
 * Reset cache
 */
int blockdev_flush(blockdev_t *dev) {
    if (!dev || !dev->flush_cache) {
        return 0; /*
 * Some devices do not support
 */
    }
    
    return dev->flush_cache(dev);
}

/*
 * Getting device information
 */
void blockdev_get_info(blockdev_t *dev, char *buffer, sz buf_size) {
    if (!dev || !buffer || buf_size == 0) {
        if (buffer && buf_size > 0) buffer[0] = '\0';
        return;
    }
    
    char size_str[32];
    char type_str[32];
    
    /*
 * Format the size
 */
    format_size(dev->total_bytes, size_str, sizeof(size_str));
    
    /*
 * Determining the type
 */
    switch (dev->type) {
        case BLOCKDEV_TYPE_IDE:
            strcpy(type_str, "IDE");
            break;
        case BLOCKDEV_TYPE_AHCI:
            strcpy(type_str, "AHCI");
            break;
        case BLOCKDEV_TYPE_RAMDISK:
            strcpy(type_str, "RAMDISK");
            break;
        default:
            strcpy(type_str, "UNKNOWN");
            break;
    }
    
    /*
 * Additional information depending on type
 */
    char extra_info[128] = "";
    
   if (dev->type == BLOCKDEV_TYPE_IDE) {
        snprintf(extra_info, sizeof(extra_info),
                "\nIDE Channel: %s, Drive: %s",
                dev->device_data.ide.channel == 0 ? "Primary" : "Secondary",
                dev->device_data.ide.drive == 0 ? "Master" : "Slave");
    }
    
    /*
 * Forming a string
 */
    snprintf(buffer, buf_size, 
             "Device: %s\n"
             "Type: %s\n"
             "Status: %s\n"
             "Size: %s\n"
             "Sector size: %lu bytes\n"
             "Total sectors: %lu\n"
             "LBA48: %s\n"
             "Stats: %lu reads, %lu writes, %lu errors%s",
             dev->name,
             type_str,
             dev->status == BLOCKDEV_READY ? "READY" : 
             dev->status == BLOCKDEV_ERROR ? "ERROR" : "NOT READY",
             size_str,
             (unsigned long)dev->sector_size,
             (unsigned long)dev->total_sectors,
             dev->supports_lba48 ? "Yes" : "No",
             (unsigned long)dev->read_count,
             (unsigned long)dev->write_count,
             (unsigned long)dev->error_count,
             extra_info);
}

/*
 * Removing a device
 */
void blockdev_unregister(blockdev_t *dev) {
    if (!dev) return;
    
    /*
 * Remove from the list
 */
    if (blockdev_list_head == dev) {
        blockdev_list_head = dev->next;
    } else {
        blockdev_t *prev = blockdev_list_head;
        while (prev && prev->next != dev) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = dev->next;
        }
    }
    
    terminal_printf("[BLOCKDEV] Unregistered device: %s\n", dev->name);
    
    /*
 * Freeing up memory
 */
    free(dev);
    blockdev_count--;
}

/*
 * Debug output of all devices
 */
void blockdev_dump_all(void) {   
    terminal_printf("\n=== Block Devices (%d total) ===\n", blockdev_count);
    
    blockdev_t *dev = blockdev_list_head;
    int index = 1;
    
    while (dev) {
        char size_str[32];
        format_size(dev->total_bytes, size_str, sizeof(size_str));
        
        const char *type_str = "UNKNOWN";
        switch (dev->type) {
            case BLOCKDEV_TYPE_IDE: type_str = "IDE"; break;
            case BLOCKDEV_TYPE_AHCI: type_str = "AHCI"; break;
            case BLOCKDEV_TYPE_RAMDISK: type_str = "RAMDISK"; break;
        }
        
        const char *status_str = "UNKNOWN";
        switch (dev->status) {
            case BLOCKDEV_UNINITIALIZED: status_str = "UNINIT"; break;
            case BLOCKDEV_READY: status_str = "READY"; break;
            case BLOCKDEV_ERROR: status_str = "ERROR"; break;
            case BLOCKDEV_NO_MEDIA: status_str = "NO MEDIA"; break;
        }
        
        terminal_printf("%d. %-8s [%-6s] %-6s %12s  Sectors: %-8lu\n",
                   index++,
                   dev->name,
                   type_str,
                   status_str,
                   size_str,
                   (unsigned long)dev->total_sectors);
        
        dev = dev->next;
    }
    
    if (blockdev_count == 0) {
        terminal_error_printf("No block devices found\n");
    }
}

/*
 * ==================== INTEGRATION WITH EXISTING DRIVERS ======
 */

/*
 * Registering an IDE drive
 */
void blockdev_register_ide(void *ide_disk_ptr, const char *name, 
                          u8 channel, u8 drive) {
    ide_disk_t *ide_disk = (ide_disk_t*)ide_disk_ptr;
    
    if (!ide_disk || ide_disk->type == IDE_TYPE_NONE) {
        return;
    }
    
    /*
 * Creating a block device
 */
    blockdev_t *bdev = blockdev_register(name, BLOCKDEV_TYPE_IDE);
    if (!bdev) return;
    
    /*
 * Filling out the information
 */
    bdev->sector_size = ide_disk->sector_size;
    bdev->total_sectors = ide_disk->total_sectors;
    bdev->total_bytes = ide_disk->total_sectors * ide_disk->sector_size;
    bdev->supports_lba48 = ide_disk->supports_lba48;
    
    /*
 * Installing handlers
 */
    bdev->read_sectors = ide_read_wrapper;
    bdev->write_sectors = ide_write_wrapper;
    bdev->flush_cache = ide_flush_wrapper;
    
    /*
 * We save specific data
 */
    bdev->device_data.ide.ide_disk = ide_disk;
    bdev->device_data.ide.channel = channel;
    bdev->device_data.ide.drive = drive;
    
    /*
 * Setting the status
 */
    bdev->status = BLOCKDEV_READY;
    
    terminal_printf("[BLOCKDEV] IDE device %s registered (%lu MB)\n", /*
 * %llu -> %lu
 */
               name, (unsigned long)(bdev->total_bytes / (1024 * 1024))); /*
 * Type casting
 */
}

/*
 * Automatically scan and register all drives
 */
void blockdev_scan_all_disks(int type) {
    terminal_printf("[BLOCKDEV] Scanning disks...\n");
    
    if (type == 1) { // IDE
    	int disk_counter = 1;
    	char dev_name[BLOCKDEV_NAME_LEN];
    
    	//1. IDE drives
    	terminal_printf("  Scanning IDE controllers...\n");
    	for (int ch = 0; ch < 2; ch++) {
            for (int dr = 0; dr < 2; dr++) {
            	int idx = ch * 2 + dr;
            	if (disks[idx].type != IDE_TYPE_NONE) {
                    snprintf(dev_name, sizeof(dev_name), "ide_%d", disk_counter++);
                    blockdev_register_ide(&disks[idx], dev_name, ch, dr);
                }
            }
    	}
    }

    if (type == 2) { // AHCI
	terminal_printf("  Scanning AHCI controllers...\n");
    	for (int i = 0; i < 32; i++) {
	        ahci_port_t *port = ahci_get_port(i);
	        if (port && port->status == AHCI_PORT_ACTIVE) {
	    	    blockdev_register_ahci(port, i);
	        }
    	}
    }

    terminal_printf("\n[BLOCKDEV] Scan complete.\n");
}
