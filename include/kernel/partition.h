#ifndef PARTITION_H
#define PARTITION_H

#include <kernel/types.h>
#include <kernel/blockdev.h>

// ==================== MBR ====================
#define PART_TYPE_EMPTY         0x00
#define PART_TYPE_FAT12         0x01
#define PART_TYPE_FAT16         0x04
#define PART_TYPE_FAT32         0x0B
#define PART_TYPE_FAT32_LBA     0x0C
#define PART_TYPE_FAT16_LBA     0x0E
#define PART_TYPE_EXFAT         0x07
#define PART_TYPE_EXT2          0x83
#define PART_TYPE_LINUX         0x83
#define PART_TYPE_SWAP          0x82
#define PART_TYPE_EXTENDED      0x05
#define PART_TYPE_EXTENDED_LBA  0x0F
#define PART_TYPE_EFI           0xEE  // Protective MBR for GPT
#define PART_TYPE_GPT           0xEE

#define PART_FLAG_BOOTABLE      0x80

// MBR Partition entry (16 bytes)
typedef struct {
    u8 status;
    u8 chs_first[3];
    u8 type;
    u8 chs_last[3];
    u32 lba_first;
    u32 sector_count;
} __attribute__((packed)) mbr_partition_t;

// MBR structure (512 bytes)
typedef struct {
    u8 bootstrap[446];
    mbr_partition_t partitions[4];
    u16 signature;
} __attribute__((packed)) mbr_t;

// ==================== GPT ====================
#define GPT_SIGNATURE           "EFI PART"
#define GPT_REVISION_1_0        0x00010000
#define GPT_HEADER_SIZE         92
#define GPT_PARTITION_ENTRY_SIZE 128

// GPT Partition types (GUIDs)
#define GPT_PART_TYPE_UNUSED        {0x00000000, 0x0000, 0x0000, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
#define GPT_PART_TYPE_EFI_SYSTEM    {0xC12A7328, 0xF81F, 0x11D2, 0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B}
#define GPT_PART_TYPE_MBR           {0x024DEE41, 0x33E7, 0x11D3, 0x9D,0x69,0x00,0x08,0xC7,0x81,0xF3,0x9F}
#define GPT_PART_TYPE_BASIC_DATA    {0xEBD0A0A2, 0xB9E5, 0x4433, 0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7}
#define GPT_PART_TYPE_LINUX_DATA    {0x0FC63DAF, 0x8483, 0x4772, 0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4}
#define GPT_PART_TYPE_LINUX_SWAP    {0x0657FD6D, 0xA4AB, 0x43C4, 0x84,0xE5,0x09,0x33,0xC8,0x4B,0x4F,0x4F}
#define GPT_PART_TYPE_MICROSOFT_RESERVED {0xE3C9E316,0x0B5C,0x4DB8,0x81,0x7D,0xF9,0x2D,0xF0,0x02,0x15,0xAE}

// GUID structure
typedef struct {
    u32 data1;
    u16 data2;
    u16 data3;
    u8 data4[8];
} __attribute__((packed)) gpt_guid_t;

// GPT Header (92 bytes, but can be larger)
typedef struct {
    u8 signature[8];
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved;
    u64 my_lba;
    u64 alternate_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    gpt_guid_t disk_guid;
    u64 partition_entry_lba;
    u32 partition_entry_count;
    u32 partition_entry_size;
    u32 partition_entry_array_crc32;
} __attribute__((packed)) gpt_header_t;

// GPT Partition entry (128 bytes)
typedef struct {
    gpt_guid_t partition_type_guid;
    gpt_guid_t unique_guid;
    u64 first_lba;
    u64 last_lba;
    u64 attributes;
    u16 name[36];  // UTF-16LE
} __attribute__((packed)) gpt_partition_entry_t;

// ==================== PARTITION STRUCTURE ====================

typedef struct partition {
    char name[32];
    blockdev_t *parent_disk;
    u32 index;
    u8 type;                    // For MBR
    gpt_guid_t type_guid;       // For GPT
    u64 start_lba;
    u64 sector_count;
    bool bootable;
    bool is_logical;
    bool is_gpt;
    char part_label[37];        // Partition label (UTF-8)
    struct partition *next;
} partition_t;

// Partition manager structure
typedef struct {
    partition_t *partitions;
    u32 count;
    bool initialized;
    bool is_gpt_disk[32];       // For each disk
    gpt_guid_t disk_guid[32];   // Disk GUID for GPT
} partition_manager_t;

// ==================== FUNCTIONS ====================

void partition_init(void);
int partition_scan_disk(blockdev_t *disk);
void partition_scan_all(void);
partition_t* partition_get_by_name(const char *name);
partition_t* partition_get_by_index(u32 index);
void partition_print_all(void);

int read_mbr(blockdev_t *disk, mbr_t *mbr);
int write_mbr(blockdev_t *disk, mbr_t *mbr);

// Partition operations
int partition_create_mbr(blockdev_t *disk, u32 index, u8 type, u64 start_lba, u64 sector_count, bool bootable);
int partition_create_gpt(blockdev_t *disk, u32 index, gpt_guid_t *type_guid, u64 start_lba, u64 sector_count, const char *label);
int partition_delete(blockdev_t *disk, u32 index);
int partition_set_bootable(blockdev_t *disk, u32 index, bool bootable);
int partition_format(partition_t *part, const char *fs_type);
int partition_read(partition_t *part, u64 lba, u32 count, void *buffer);
int partition_write(partition_t *part, u64 lba, u32 count, const void *buffer);

// Helper functions
const char* partition_type_to_string(partition_t *part);
u64 partition_get_size_mb(partition_t *part);
blockdev_t* partition_get_blockdev(partition_t *part);
bool disk_is_gpt(blockdev_t *disk);
int disk_create_gpt(blockdev_t *disk);

// GUID helpers
int guid_to_string(gpt_guid_t *guid, char *str, int max_len);
int string_to_guid(const char *str, gpt_guid_t *guid);
int guid_compare(const gpt_guid_t *a, const gpt_guid_t *b);
const char* guid_to_type_string(gpt_guid_t *guid);

#endif
