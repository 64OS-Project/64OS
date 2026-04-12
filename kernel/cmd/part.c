#include <kernel/types.h>
#include <kernel/terminal.h>
#include <kernel/partition.h>
#include <kernel/blockdev.h>
#include <libk/string.h>
#include <fs/vfs.h>

void cmd_part_list(void) {
    partition_print_all();
}

void cmd_part_scan(void) {
    terminal_printf("Scanning all disks for partitions...\n");
    partition_scan_all();
    terminal_success_printf("Scan complete\n");
    cmd_part_list();
}

void cmd_part_info(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: part info <partition>\n");
        terminal_printf("Example: part info sata_0:1\n");
        return;
    }
    
    while (*args == ' ') args++;
    
    partition_t *part = partition_get_by_name(args);
    if (!part) {
        terminal_error_printf("Partition '%s' not found\n", args);
        return;
    }
    
    terminal_printf("\n=== Partition Info ===\n");
    terminal_printf("Name: %s\n", part->name);
    terminal_printf("Device: %s\n", part->parent_disk->name);
    terminal_printf("Index: %d\n", part->index);
    terminal_printf("Type: %s\n", partition_type_to_string(part));
    terminal_printf("Start LBA: %llu\n", (unsigned long long)part->start_lba);
    terminal_printf("Sectors: %llu\n", (unsigned long long)part->sector_count);
    terminal_printf("Size: %llu MB\n", (unsigned long long)partition_get_size_mb(part));
    terminal_printf("Bootable: %s\n", part->bootable ? "Yes" : "No");
    terminal_printf("GPT: %s\n", part->is_gpt ? "Yes" : "No");
    if (part->is_gpt && part->part_label[0]) {
        terminal_printf("Label: %s\n", part->part_label);
    }
    terminal_printf("=====================\n");
}

void cmd_part_create(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: part create <disk> <type> <size_mb> [bootable]\n");
        terminal_printf("Types: exfat, fat32, linux, swap\n");
        terminal_printf("Example: part create sata_0 exfat 1024\n");
        terminal_printf("Example: part create ide_0 fat32 512 bootable\n");
        return;
    }
    
    char *disk_name = strtok_r(args, " ", &args);
    char *type_str = strtok_r(NULL, " ", &args);
    char *size_str = strtok_r(NULL, " ", &args);
    char *bootable_str = strtok_r(NULL, " ", &args);
    
    if (!disk_name || !type_str || !size_str) {
        terminal_error_printf("Missing arguments\n");
        return;
    }
    
    blockdev_t *disk = blockdev_find(disk_name);
    if (!disk) {
        terminal_error_printf("Disk '%s' not found\n", disk_name);
        return;
    }
    
    u64 size_mb = atoi(size_str);
    u64 sector_count = (size_mb * 1024 * 1024) / disk->sector_size;
    
    u8 part_type = 0;
    if (strcmp(type_str, "exfat") == 0) {
        part_type = PART_TYPE_EXFAT;
    } else if (strcmp(type_str, "fat32") == 0) {
        part_type = PART_TYPE_FAT32_LBA;
    } else if (strcmp(type_str, "linux") == 0) {
        part_type = PART_TYPE_LINUX;
    } else if (strcmp(type_str, "swap") == 0) {
        part_type = PART_TYPE_SWAP;
    } else {
        terminal_error_printf("Unknown type: %s\n", type_str);
        return;
    }
    
    // Find first free slot
    mbr_t mbr;
    if (blockdev_read(disk, 0, 1, &mbr) != 0) {
        terminal_error_printf("Failed to read MBR\n");
        return;
    }
    
    int free_slot = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.partitions[i].type == PART_TYPE_EMPTY) {
            free_slot = i;
            break;
        }
    }
    
    if (free_slot == -1) {
        terminal_error_printf("No free partition slots (max 4 primary)\n");
        return;
    }
    
    // Calculate start LBA (after existing partitions)
    u64 start_lba = 2048; // 1MB alignment
    for (int i = 0; i < free_slot; i++) {
        if (mbr.partitions[i].lba_first + mbr.partitions[i].sector_count > start_lba) {
            start_lba = mbr.partitions[i].lba_first + mbr.partitions[i].sector_count;
        }
    }
    
    bool bootable = (bootable_str && strcmp(bootable_str, "bootable") == 0);
    
    if (partition_create_mbr(disk, free_slot + 1, part_type, start_lba, sector_count, bootable) == 0) {
        terminal_success_printf("Partition created\n");
        partition_scan_disk(disk);
    } else {
        terminal_error_printf("Failed to create partition\n");
    }
}

void cmd_part_delete(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: part delete <disk> <num>\n");
        terminal_printf("Example: part delete sata_0 1\n");
        return;
    }
    
    char *disk_name = strtok_r(args, " ", &args);
    char *num_str = strtok_r(NULL, " ", &args);
    
    if (!disk_name || !num_str) {
        terminal_error_printf("Missing arguments\n");
        return;
    }
    
    blockdev_t *disk = blockdev_find(disk_name);
    if (!disk) {
        terminal_error_printf("Disk '%s' not found\n", disk_name);
        return;
    }
    
    u32 index = atoi(num_str);
    if (index < 1 || index > 4) {
        terminal_error_printf("Invalid partition number (1-4)\n");
        return;
    }
    
    terminal_printf("WARNING: This will delete partition %d on %s!\n", index, disk_name);
    
    char *confirm = terminal_input("Type 'YES' to continue: ");
    if (!confirm || strcmp(confirm, "YES") != 0) {
        terminal_printf("Operation cancelled\n");
        return;
    }
    
    if (partition_delete(disk, index) == 0) {
        terminal_success_printf("Partition deleted\n");
        partition_scan_disk(disk);
    } else {
        terminal_error_printf("Failed to delete partition\n");
    }
}

void cmd_part_gpt(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: part gpt <disk>\n");
        terminal_printf("Example: part gpt sata_0\n");
        return;
    }
    
    while (*args == ' ') args++;
    
    blockdev_t *disk = blockdev_find(args);
    if (!disk) {
        terminal_error_printf("Disk '%s' not found\n", args);
        return;
    }
    
    terminal_printf("WARNING: This will create GPT on %s and DESTROY all data!\n", args);
    
    char *confirm = terminal_input("Type 'YES' to continue: ");
    if (!confirm || strcmp(confirm, "YES") != 0) {
        terminal_printf("Operation cancelled\n");
        return;
    }
    
    if (disk_create_gpt(disk) == 0) {
        terminal_success_printf("GPT created on %s\n", args);
        partition_scan_disk(disk);
    } else {
        terminal_error_printf("Failed to create GPT\n");
    }
}

void cmd_part_mount(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: part mount <partition> [mountpoint]\n");
        terminal_printf("Example: part mount sata_0:1 /mnt\n");
        return;
    }
    
    char *part_name = strtok_r(args, " ", &args);
    char *mountpoint = strtok_r(NULL, " ", &args);
    
    if (!part_name) {
        terminal_error_printf("Missing partition name\n");
        return;
    }
    
    partition_t *part = partition_get_by_name(part_name);
    if (!part) {
        terminal_error_printf("Partition '%s' not found\n", part_name);
        return;
    }
    
    if (!mountpoint) {
        mountpoint = "/mnt";
    }
    
    // Get blockdev for partition
    blockdev_t *part_dev = partition_get_blockdev(part);
    if (!part_dev) {
        terminal_error_printf("Failed to create blockdev for partition\n");
        return;
    }
    
    // Try to mount filesystem
    vfs_inode_t *root;
    const char *fs_type = (part->type == PART_TYPE_EXFAT) ? "exfat" : "fat32";
    
    if (vfs_mount(fs_type, part_dev, &root) == 0) {
        vfs_mount_point(mountpoint, root);
        terminal_success_printf("Mounted %s at %s\n", part_name, mountpoint);
    } else {
        terminal_error_printf("Failed to mount %s (wrong filesystem?)\n", part_name);
    }
}
