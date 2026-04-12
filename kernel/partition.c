#include <kernel/partition.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <mm/heap.h>
#include <fs/exfat.h>
#include <fs/fat32.h>

static partition_manager_t g_part_mgr = {0};

// ==================== GUID DEFINITIONS ====================

static const struct {
    gpt_guid_t guid;
    const char *name;
} g_guid_types[] = {
    {GPT_PART_TYPE_EFI_SYSTEM, "EFI System"},
    {GPT_PART_TYPE_BASIC_DATA, "Basic Data"},
    {GPT_PART_TYPE_LINUX_DATA, "Linux Data"},
    {GPT_PART_TYPE_LINUX_SWAP, "Linux Swap"},
    {GPT_PART_TYPE_MICROSOFT_RESERVED, "Microsoft Reserved"},
};

// ==================== GUID HELPER FUNCTIONS ====================

static void guid_init(gpt_guid_t *guid, u32 d1, u16 d2, u16 d3, 
                      u8 d41, u8 d42, u8 d43, u8 d44, u8 d45, u8 d46, u8 d47) {
    guid->data1 = d1;
    guid->data2 = d2;
    guid->data3 = d3;
    guid->data4[0] = d41;
    guid->data4[1] = d42;
    guid->data4[2] = d43;
    guid->data4[3] = d44;
    guid->data4[4] = d45;
    guid->data4[5] = d46;
    guid->data4[6] = d47;
    guid->data4[7] = 0;
}

int guid_to_string(gpt_guid_t *guid, char *str, int max_len) {
    if (!guid || !str || max_len < 37) return -1;
    snprintf(str, max_len, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             guid->data1, guid->data2, guid->data3,
             guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
             guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
    return 0;
}

int guid_compare(const gpt_guid_t *a, const gpt_guid_t *b) {
    if (a->data1 != b->data1) return 0;
    if (a->data2 != b->data2) return 0;
    if (a->data3 != b->data3) return 0;
    return memcmp(a->data4, b->data4, 8) == 0;
}

const char* guid_to_type_string(gpt_guid_t *guid) {
    for (int i = 0; i < sizeof(g_guid_types) / sizeof(g_guid_types[0]); i++) {
        if (guid_compare(guid, &g_guid_types[i].guid)) {
            return g_guid_types[i].name;
        }
    }
    return "Unknown";
}

//Section read/write wrappers
static int part_read_wrapper(blockdev_t *bdev, u64 lba, u32 count, void *buffer) {
    partition_t *part = (partition_t*)bdev->device_data.ahci.ahci_port;
    return partition_read(part, lba, count, buffer);
}

static int part_write_wrapper(blockdev_t *bdev, u64 lba, u32 count, const void *buffer) {
    partition_t *part = (partition_t*)bdev->device_data.ahci.ahci_port;
    return partition_write(part, lba, count, buffer);
}

// ==================== GPT FUNCTIONS ====================

static u32 calculate_crc32(void *data, u32 size) {
    //Simple CRC32 implementation (can be replaced with a faster one)
    u32 crc = 0xFFFFFFFF;
    u8 *bytes = (u8*)data;
    
    for (u32 i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return ~crc;
}

static int read_gpt_header(blockdev_t *disk, gpt_header_t *header, u64 lba) {
    if (blockdev_read(disk, lba, 1, header) != 0) return -1;
    if (memcmp(header->signature, GPT_SIGNATURE, 8) != 0) return -1;
    if (header->revision != GPT_REVISION_1_0) return -1;
    
    // Verify CRC32
    u32 crc = calculate_crc32(header, header->header_size);
    if (crc != header->header_crc32) return -1;
    
    return 0;
}

static int write_gpt_header(blockdev_t *disk, gpt_header_t *header, u64 lba) {
    header->header_crc32 = 0;
    header->header_crc32 = calculate_crc32(header, header->header_size);
    return blockdev_write(disk, lba, 1, header);
}

static int read_gpt_partition_entries(blockdev_t *disk, gpt_header_t *header, 
                                       gpt_partition_entry_t **entries) {
    u32 table_size = header->partition_entry_count * header->partition_entry_size;
    u32 sectors = (table_size + disk->sector_size - 1) / disk->sector_size;
    
    *entries = (gpt_partition_entry_t*)malloc(table_size);
    if (!*entries) return -1;
    
    if (blockdev_read(disk, header->partition_entry_lba, sectors, *entries) != 0) {
        free(*entries);
        return -1;
    }
    
    // Verify CRC32
    u32 crc = calculate_crc32(*entries, table_size);
    if (crc != header->partition_entry_array_crc32) {
        free(*entries);
        return -1;
    }
    
    return 0;
}

static void utf16le_to_utf8(u16 *src, char *dst, int max_len) {
    int i = 0;
    while (src[i] && i < 35 && i < max_len - 1) {
        if (src[i] < 0x80) {
            dst[i] = (char)src[i];
        } else {
            dst[i] = '?';
        }
        i++;
    }
    dst[i] = '\0';
}

bool disk_is_gpt(blockdev_t *disk) {
    if (!disk) return false;
    
    mbr_t mbr;
    if (read_mbr(disk, &mbr) != 0) return false;
    
    // Check for protective MBR
    if (mbr.partitions[0].type == PART_TYPE_EFI && 
        mbr.partitions[0].lba_first == 1) {
        return true;
    }
    return false;
}

int disk_create_gpt(blockdev_t *disk) {
    if (!disk) return -1;
    
    terminal_printf("[GPT] Creating GPT on %s...\n", disk->name);
    
    // Create protective MBR
    mbr_t mbr;
    memset(&mbr, 0, sizeof(mbr_t));
    mbr.partitions[0].type = PART_TYPE_EFI;
    mbr.partitions[0].lba_first = 1;
    mbr.partitions[0].sector_count = 0xFFFFFFFF;
    mbr.signature = 0xAA55;
    
    if (write_mbr(disk, &mbr) != 0) return -1;
    
    // Create GPT header (at LBA 1)
    gpt_header_t header;
    memset(&header, 0, sizeof(gpt_header_t));
    memcpy(header.signature, GPT_SIGNATURE, 8);
    header.revision = GPT_REVISION_1_0;
    header.header_size = GPT_HEADER_SIZE;
    header.my_lba = 1;
    header.alternate_lba = disk->total_sectors - 1;
    header.first_usable_lba = 34;
    header.last_usable_lba = disk->total_sectors - 34;
    header.partition_entry_lba = 2;
    header.partition_entry_count = 128;
    header.partition_entry_size = GPT_PARTITION_ENTRY_SIZE;
    
    // Generate random disk GUID (simplified)
    header.disk_guid.data1 = 0x12345678;
    header.disk_guid.data2 = 0x9ABC;
    header.disk_guid.data3 = 0xDEF0;
    memset(header.disk_guid.data4, 0, 8);
    
    if (write_gpt_header(disk, &header, 1) != 0) return -1;
    
    // Write backup GPT at end of disk
    header.my_lba = disk->total_sectors - 1;
    header.alternate_lba = 1;
    if (write_gpt_header(disk, &header, disk->total_sectors - 1) != 0) return -1;
    
    // Create empty partition table at LBA 2-33
    u8 *empty_table = (u8*)malloc(32 * disk->sector_size);
    if (!empty_table) return -1;
    memset(empty_table, 0, 32 * disk->sector_size);
    
    if (blockdev_write(disk, 2, 32, empty_table) != 0) {
        free(empty_table);
        return -1;
    }
    free(empty_table);
    
    terminal_success_printf("[GPT] GPT created on %s\n", disk->name);
    return 0;
}

static int scan_gpt(blockdev_t *disk) {
    gpt_header_t header;
    if (read_gpt_header(disk, &header, 1) != 0) {
        return -1;
    }
    
    gpt_partition_entry_t *entries;
    if (read_gpt_partition_entries(disk, &header, &entries) != 0) {
        return -1;
    }
    
    for (u32 i = 0; i < header.partition_entry_count; i++) {
        gpt_partition_entry_t *entry = &entries[i];
        
        if (entry->first_lba == 0 && entry->last_lba == 0) continue;
        
        char label[37];
        utf16le_to_utf8(entry->name, label, sizeof(label));
        
        partition_t *part = (partition_t*)malloc(sizeof(partition_t));
        if (!part) break;
        
        memset(part, 0, sizeof(partition_t));
        
        // Generate name
        snprintf(part->name, sizeof(part->name), "%s:%d", disk->name, i + 1);
        
        part->parent_disk = disk;
        part->index = i + 1;
        part->type_guid = entry->partition_type_guid;
        part->start_lba = entry->first_lba;
        part->sector_count = entry->last_lba - entry->first_lba + 1;
        part->is_gpt = true;
        strncpy(part->part_label, label, sizeof(part->part_label) - 1);
        
        // Add to list
        part->next = g_part_mgr.partitions;
        g_part_mgr.partitions = part;
        g_part_mgr.count++;
        
        terminal_printf("[GPT] Partition %d: %s, type=%s, start=%llu, size=%llu MB\n",
                       i + 1, label, guid_to_type_string(&entry->partition_type_guid),
                       (unsigned long long)entry->first_lba,
                       (unsigned long long)((entry->last_lba - entry->first_lba + 1) * 512 / (1024 * 1024)));
    }
    
    free(entries);
    return 0;
}

// ==================== MBR FUNCTIONS ====================

int read_mbr(blockdev_t *disk, mbr_t *mbr) {
    return blockdev_read(disk, 0, 1, mbr);
}

int write_mbr(blockdev_t *disk, mbr_t *mbr) {
    return blockdev_write(disk, 0, 1, mbr);
}

static int is_valid_mbr(mbr_t *mbr) {
    return mbr->signature == 0xAA55;
}

static void add_partition(blockdev_t *disk, u32 index, u8 type, u64 start_lba, u64 sector_count, bool bootable) {
    if (g_part_mgr.count >= 128) return;
    
    partition_t *part = (partition_t*)malloc(sizeof(partition_t));
    if (!part) return;
    
    memset(part, 0, sizeof(partition_t));
    
    snprintf(part->name, sizeof(part->name), "%s:%d", disk->name, index);
    
    part->parent_disk = disk;
    part->index = index;
    part->type = type;
    part->start_lba = start_lba;
    part->sector_count = sector_count;
    part->bootable = bootable;
    part->is_logical = (index > 4);
    part->is_gpt = false;
    
    part->next = g_part_mgr.partitions;
    g_part_mgr.partitions = part;
    g_part_mgr.count++;
}

static void scan_mbr(blockdev_t *disk, mbr_t *mbr) {
    for (int i = 0; i < 4; i++) {
        mbr_partition_t *entry = &mbr->partitions[i];
        
        if (entry->type == PART_TYPE_EMPTY) continue;
        
        bool bootable = (entry->status == PART_FLAG_BOOTABLE);
        add_partition(disk, i + 1, entry->type, entry->lba_first, entry->sector_count, bootable);
        
        if (entry->type == PART_TYPE_EXTENDED || entry->type == PART_TYPE_EXTENDED_LBA) {
            u32 current_lba = entry->lba_first;
            int logical_index = 5;
            
            while (current_lba != 0) {
                u8 ebr[512];
                if (blockdev_read(disk, current_lba, 1, ebr) != 0) break;
                
                mbr_partition_t *logical = (mbr_partition_t*)(ebr + 446);
                
                if (logical->type != PART_TYPE_EMPTY) {
                    add_partition(disk, logical_index++, logical->type, 
                                  current_lba + logical->lba_first, 
                                  logical->sector_count, false);
                }
                
                mbr_partition_t *next = (mbr_partition_t*)(ebr + 446 + 16);
                if (next->lba_first == 0) break;
                current_lba = entry->lba_first + next->lba_first;
            }
        }
    }
}

// ==================== PUBLIC FUNCTIONS ====================

void partition_init(void) {
    memset(&g_part_mgr, 0, sizeof(partition_manager_t));
    g_part_mgr.initialized = true;
    terminal_printf("[PART] Partition manager initialized (GPT + MBR)\n");
}

int partition_scan_disk(blockdev_t *disk) {
    if (!disk || disk->status != BLOCKDEV_READY) return -1;
    
    terminal_printf("[PART] Scanning %s...\n", disk->name);
    
    // Check for GPT first
    if (disk_is_gpt(disk)) {
        terminal_printf("[PART] GPT detected on %s\n", disk->name);
        return scan_gpt(disk);
    }
    
    // Fallback to MBR
    mbr_t mbr;
    if (read_mbr(disk, &mbr) != 0) {
        terminal_error_printf("[PART] Failed to read MBR from %s\n", disk->name);
        return -1;
    }
    
    if (!is_valid_mbr(&mbr)) {
        terminal_warn_printf("[PART] No valid partition table on %s\n", disk->name);
        return 0;
    }
    
    scan_mbr(disk, &mbr);
    return 0;
}

void partition_scan_all(void) {
    blockdev_t *disks[32];
    int count = blockdev_get_list(disks, 32);
    
    terminal_printf("[PART] Scanning %d disks...\n", count);
    
    for (int i = 0; i < count; i++) {
        if (disks[i] && disks[i]->status == BLOCKDEV_READY) {
            partition_scan_disk(disks[i]);
        }
    }
}

partition_t* partition_get_by_name(const char *name) {
    partition_t *part = g_part_mgr.partitions;
    while (part) {
        if (strcmp(part->name, name) == 0) {
            return part;
        }
        part = part->next;
    }
    return NULL;
}

partition_t* partition_get_by_index(u32 index) {
    partition_t *part = g_part_mgr.partitions;
    u32 i = 0;
    while (part) {
        if (i == index) return part;
        part = part->next;
        i++;
    }
    return NULL;
}

void partition_print_all(void) {
    terminal_printf("\n=== Partitions (%d total) ===\n", g_part_mgr.count);
    terminal_printf("%-4s %-12s %-10s %-12s %-12s %s\n", 
                   "No.", "Name", "Type", "Start (LBA)", "Size (MB)", "Label");
    terminal_printf("---- ------------ ---------- ------------ ------------ -----------------\n");
    
    partition_t *part = g_part_mgr.partitions;
    int i = 0;
    while (part) {
        const char *type_str;
        if (part->is_gpt) {
            type_str = guid_to_type_string(&part->type_guid);
        } else {
            type_str = partition_type_to_string(part);
        }
        
        terminal_printf("%-4d %-12s %-10s %-12llu %-12llu %s\n",
                       i + 1, part->name, type_str,
                       (unsigned long long)part->start_lba,
                       (unsigned long long)partition_get_size_mb(part),
                       part->part_label);
        part = part->next;
        i++;
    }
    terminal_printf("===============================\n");
}

const char* partition_type_to_string(partition_t *part) {
    if (!part) return "Unknown";
    if (part->is_gpt) return guid_to_type_string(&part->type_guid);
    
    switch(part->type) {
        case PART_TYPE_EMPTY:       return "Empty";
        case PART_TYPE_FAT12:       return "FAT12";
        case PART_TYPE_FAT16:       return "FAT16";
        case PART_TYPE_FAT32:       return "FAT32";
        case PART_TYPE_FAT32_LBA:   return "FAT32 LBA";
        case PART_TYPE_FAT16_LBA:   return "FAT16 LBA";
        case PART_TYPE_EXFAT:       return "exFAT";
        case PART_TYPE_EXT2:        return "Linux";
        case PART_TYPE_SWAP:        return "Swap";
        default:                    return "Unknown";
    }
}

u64 partition_get_size_mb(partition_t *part) {
    if (!part) return 0;
    return (part->sector_count * 512) / (1024 * 1024);
}

int partition_read(partition_t *part, u64 lba, u32 count, void *buffer) {
    if (!part || !part->parent_disk) return -1;
    return blockdev_read(part->parent_disk, part->start_lba + lba, count, buffer);
}

int partition_write(partition_t *part, u64 lba, u32 count, const void *buffer) {
    if (!part || !part->parent_disk) return -1;
    return blockdev_write(part->parent_disk, part->start_lba + lba, count, buffer);
}

int partition_create_mbr(blockdev_t *disk, u32 index, u8 type, u64 start_lba, u64 sector_count, bool bootable) {
    if (!disk || index < 1 || index > 4) return -1;
    
    mbr_t mbr;
    if (read_mbr(disk, &mbr) != 0) return -1;
    
    mbr_partition_t *entry = &mbr.partitions[index - 1];
    entry->status = bootable ? PART_FLAG_BOOTABLE : 0;
    entry->type = type;
    entry->lba_first = start_lba;
    entry->sector_count = sector_count;
    entry->chs_first[0] = 0;
    entry->chs_first[1] = 0;
    entry->chs_first[2] = 0;
    entry->chs_last[0] = 0;
    entry->chs_last[1] = 0;
    entry->chs_last[2] = 0;
    
    if (write_mbr(disk, &mbr) != 0) return -1;
    
    terminal_success_printf("[PART] Created MBR partition %d on %s (type=0x%02x, bootable=%d)\n",
                           index, disk->name, type, bootable);
    
    return 0;
}
int partition_delete_mbr(blockdev_t *disk, u32 index) {
    if (!disk || index < 1 || index > 4) return -1;
    
    mbr_t mbr;
    if (read_mbr(disk, &mbr) != 0) return -1;
    
    memset(&mbr.partitions[index - 1], 0, sizeof(mbr_partition_t));
    
    if (write_mbr(disk, &mbr) != 0) return -1;
    
    return 0;
}

blockdev_t* partition_get_blockdev(partition_t *part) {
    if (!part) return NULL;
    
    char dev_name[32];
    snprintf(dev_name, sizeof(dev_name), "%s", part->name);
    
    blockdev_t *bdev = blockdev_register(dev_name, BLOCKDEV_TYPE_NONE);
    if (!bdev) return NULL;
    
    bdev->sector_size = part->parent_disk->sector_size;
    bdev->total_sectors = part->sector_count;
    bdev->total_bytes = bdev->sector_size * part->sector_count;
    bdev->status = BLOCKDEV_READY;
    bdev->read_sectors = part_read_wrapper;
    bdev->write_sectors = part_write_wrapper;
    bdev->device_data.ahci.ahci_port = part;
    
    return bdev;
}

int partition_delete(blockdev_t *disk, u32 index) {
    if (!disk || index < 1 || index > 4) return -1;
    
    mbr_t mbr;
    if (read_mbr(disk, &mbr) != 0) return -1;
    
    if (mbr.signature != 0xAA55) return -1;
    
    memset(&mbr.partitions[index - 1], 0, sizeof(mbr_partition_t));
    
    if (write_mbr(disk, &mbr) != 0) return -1;
    
    terminal_success_printf("[PART] Deleted partition %d on %s\n", index, disk->name);
    return 0;
}
