#include <kdisk/cdrom.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <mm/heap.h>
#include <asm/io.h>
#include <asm/cpu.h>
#include <kernel/driver.h>

static cdrom_t g_cdrom = {0};

driver_t g_cdrom_driver = {
    .name = "cdrom",
    .desc = "CD-ROM Driver",
    .critical_level = DRIVER_CRITICAL_3,
    .probe = NULL,
    .init = NULL,
    .remove = NULL
};

//==================== AUXILIARY FUNCTIONS ====================

static void ide_delay(ide_disk_t *ide) {
    inb(ide->base_port + IDE_ALTSTATUS);
    inb(ide->base_port + IDE_ALTSTATUS);
    inb(ide->base_port + IDE_ALTSTATUS);
    inb(ide->base_port + IDE_ALTSTATUS);
}

static int ide_wait_bsy(ide_disk_t *ide, int timeout_ms) {
    for (int i = 0; i < timeout_ms * 100; i++) {
        u8 status = inb(ide->base_port + IDE_STATUS);
        if (!(status & IDE_STATUS_BSY)) return 0;
        ide_delay(ide);
    }
    return -1;
}

static int ide_wait_drq(ide_disk_t *ide, int timeout_ms) {
    for (int i = 0; i < timeout_ms * 100; i++) {
        u8 status = inb(ide->base_port + IDE_STATUS);
        if (status & IDE_STATUS_ERR) return -2;
        if (!(status & IDE_STATUS_BSY) && (status & IDE_STATUS_DRQ)) return 0;
        ide_delay(ide);
    }
    return -1;
}

//==================== ATAPI COMMANDS ====================

static int atapi_packet_command(ide_disk_t *ide, u8 *packet, u16 packet_len,
                                  void *buffer, u32 buffer_len) {
    if (ide_wait_bsy(ide, 1000) != 0) return -1;
    
    outb(ide->base_port + IDE_SELECT, (0xA0) | (ide->drive << 4));
    ide_delay(ide);
    
    outb(ide->ctrl_port + IDE_CONTROL, 0x02);
    
    outb(ide->base_port + IDE_FEATURE, 0);
    outb(ide->base_port + IDE_NSECT, 0);
    outb(ide->base_port + IDE_SECTOR, 0);
    outb(ide->base_port + IDE_LCYL, packet_len);
    outb(ide->base_port + IDE_HCYL, packet_len >> 8);
    outb(ide->base_port + IDE_COMMAND, IDE_CMD_IDENTIFY_PACKET);
    
    ide_delay(ide);
    
    int ret = ide_wait_drq(ide, 1000);
    if (ret != 0) return ret;
    
    for (int i = 0; i < packet_len / 2; i++) {
        outw(ide->base_port + IDE_DATA, ((u16*)packet)[i]);
    }
    
    ide_delay(ide);
    
    if (buffer && buffer_len > 0) {
        ret = ide_wait_drq(ide, 10000);
        if (ret != 0) return ret;
        
        u16 *buf16 = (u16*)buffer;
        u32 words = buffer_len / 2;
        for (u32 i = 0; i < words; i++) {
            buf16[i] = inw(ide->base_port + IDE_DATA);
        }
    }
    
    outb(ide->ctrl_port + IDE_CONTROL, 0x00);
    
    return 0;
}

static int atapi_read_sectors(ide_disk_t *ide, u32 lba, u32 count, void *buffer) {
    u8 packet[12] = {
        0xA8,  // READ (10)
        0x00,
        (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
        0x00,
        (u8)(count >> 8), (u8)count,
        0x00, 0x00
    };
    
    return atapi_packet_command(ide, packet, 12, buffer, count * 2048);
}

//==================== ISO9660 CHECK ====================

static int is_primary_volume_descriptor(u8 *sector) {
    if (sector[0] != 0x01) return 0;
    if (sector[1] != 0x43 || sector[2] != 0x44 || sector[3] != 0x30 || 
        sector[4] != 0x30 || sector[5] != 0x31) {
        return 0;
    }
    return 1;
}

static int check_iso9660(ide_disk_t *ide) {
    u8 sector[2048];
    
    //Trying to read PVD with LBA 16
    if (atapi_read_sectors(ide, 16, 1, sector) == 0) {
        if (is_primary_volume_descriptor(sector)) {
            return 1;
        }
    }
    
    return 0;
}

//==================== INTERACTIVE DEFINITION ====================

static int ask_user_yes_no(const char *question) {
    char response[10];
    // terminal_printf("%s (y/n): ", question);
    char buf[128];
    snprintf(buf, sizeof(buf), "%s (y/n): ", question);
    
    char *input = terminal_input(buf);
    if (input && (input[0] == 'y' || input[0] == 'Y')) {
        return 1;
    }
    return 0;
}

cdrom_t* cdrom_find_and_init(void) {
    terminal_printf("\n=== CD-ROM Detection ===\n");
    
    blockdev_t *disks[32];
    int count = blockdev_get_list(disks, 32);
    
    terminal_printf("Available disks:\n");
    for (int i = 0; i < count; i++) {
        u64 size_mb = disks[i]->total_bytes / (1024 * 1024);
        terminal_printf("  %d: %s - %lu MB (type=%d)\n", 
                       i, disks[i]->name, (unsigned long)size_mb, disks[i]->type);
    }
    terminal_printf("\n");
    
    for (int i = 0; i < count; i++) {
        if (!disks[i]) continue;
        
        u64 size_mb = disks[i]->total_bytes / (1024 * 1024);
        int is_atapi = 0;
        
        //Checking if this device is ATAPI
        if (disks[i]->type == BLOCKDEV_TYPE_IDE) {
            ide_disk_t *ide = (ide_disk_t*)disks[i]->device_data.ide.ide_disk;
            if (ide && ide->type == IDE_TYPE_ATAPI) {
                is_atapi = 1;
            }
        }
        
        //Small disk (less than 100MB) or ATAPI
        int is_small = (size_mb < 100 && size_mb > 0) || is_atapi;
        
        if (is_small) {
            terminal_printf("[CDROM] Candidate: %s (%lu MB, ATAPI=%s)\n",
                           disks[i]->name, (unsigned long)size_mb, is_atapi ? "yes" : "no");
            
            //Checking ISO9660
            if (disks[i]->type == BLOCKDEV_TYPE_IDE) {
                ide_disk_t *ide = (ide_disk_t*)disks[i]->device_data.ide.ide_disk;
                if (ide && check_iso9660(ide)) {
                    terminal_printf("[CDROM] ISO9660 detected on %s\n", disks[i]->name);
                    if (ask_user_yes_no("Is this the CD-ROM drive?")) {
                        if (cdrom_init(disks[i]) == 0) {
                            return &g_cdrom;
                        }
                    }
                } else {
                    terminal_printf("[CDROM] No ISO9660 detected on %s\n", disks[i]->name);
                    if (ask_user_yes_no("Is this the CD-ROM drive?")) {
                        if (cdrom_init(disks[i]) == 0) {
                            terminal_warn_printf("[CDROM] No disc in drive or not a CD-ROM\n");
                            return &g_cdrom;
                        }
                    }
                }
            }
        }
    }
    
    //If not found automatically, ask the user
    terminal_printf("\n[CDROM] No CD-ROM detected automatically.\n");
    for (int i = 0; i < count; i++) {
        if (!disks[i]) continue;
        u64 size_mb = disks[i]->total_bytes / (1024 * 1024);
        
        char question[256];
        snprintf(question, sizeof(question), "Is %s (%lu MB) your CD-ROM?", 
                disks[i]->name, (unsigned long)size_mb);
        
        if (ask_user_yes_no(question)) {
            if (cdrom_init(disks[i]) == 0) {
                return &g_cdrom;
            }
        }
    }
    
    terminal_error_printf("[CDROM] No CD-ROM selected\n");
    return NULL;
}

//==================== BASIC FUNCTIONS OF CDROM ====================

int cdrom_init(blockdev_t *device) {
    if (!device) return -1;
    
    terminal_printf("[CDROM] Initializing %s as CD-ROM...\n", device->name);
    
    memset(&g_cdrom, 0, sizeof(cdrom_t));
    g_cdrom.device = device;
    g_cdrom.sector_size = 2048;
    
    //Get an IDE disk if you have one
    if (device->type == BLOCKDEV_TYPE_IDE) {
        g_cdrom.ide_disk = (ide_disk_t*)device->device_data.ide.ide_disk;
        if (g_cdrom.ide_disk) {
            g_cdrom.is_atapi = (g_cdrom.ide_disk->type == IDE_TYPE_ATAPI);
        }
    }
    
    //Trying to read ISO9660
    if (g_cdrom.ide_disk && check_iso9660(g_cdrom.ide_disk)) {
        u8 pvd[2048];
        if (atapi_read_sectors(g_cdrom.ide_disk, 16, 1, pvd) == 0) {
            //Reading the volume label (offset 40)
            memcpy(g_cdrom.volume_label, pvd + 40, 32);
            g_cdrom.volume_label[32] = '\0';
            //Trimming spaces
            for (int i = 31; i >= 0; i--) {
                if (g_cdrom.volume_label[i] == ' ') g_cdrom.volume_label[i] = '\0';
                else break;
            }
            
            //Reading volume size (offset 80)
            g_cdrom.total_sectors = *(u32*)(pvd + 80);
        }
    }
    
    //If you didn’t get the size from ISO9660, set the default value
    if (g_cdrom.total_sectors == 0) {
        g_cdrom.total_sectors = 350000;  // ~700MB
    }
    
    g_cdrom.initialized = true;
    terminal_success_printf("[CD-ROM] Ready: %s (%lu MB)\n", 
                           g_cdrom.volume_label[0] ? g_cdrom.volume_label : "Unknown",
                           (unsigned long)(g_cdrom.total_sectors * 2048 / (1024 * 1024)));
    
    return 0;
}

int cdrom_read_sectors(cdrom_t *cdrom, u32 lba, u32 count, void *buffer) {
    if (!cdrom || !cdrom->initialized || !buffer || count == 0) return -1;
    
    if (cdrom->device->type == BLOCKDEV_TYPE_IDE && cdrom->ide_disk) {
        return atapi_read_sectors(cdrom->ide_disk, lba, count, buffer);
    }
    
    return -1;
}

int cdrom_read_sector(cdrom_t *cdrom, u32 lba, void *buffer) {
    return cdrom_read_sectors(cdrom, lba, 1, buffer);
}

void cdrom_print_info(cdrom_t *cdrom) {
    if (!cdrom || !cdrom->initialized) {
        terminal_printf("[CD-ROM] Not initialized\n");
        return;
    }
    
    terminal_printf("\n=== CD-ROM Info ===\n");
    terminal_printf("Device: %s\n", cdrom->device->name);
    terminal_printf("Volume label: %s\n", cdrom->volume_label[0] ? cdrom->volume_label : "(none)");
    terminal_printf("Total size: %lu MB\n", 
                   (unsigned long)(cdrom->total_sectors * 2048 / (1024 * 1024)));
    terminal_printf("ATAPI: %s\n", cdrom->is_atapi ? "Yes" : "No");
    terminal_printf("==================\n");
}
