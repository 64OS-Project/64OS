#ifndef EXFAT_H
#define EXFAT_H

#include <kernel/types.h>
#include <fs/vfs.h>

//exFAT signature
#define EXFAT_SIGNATURE     0x7C8E4FD2A5C14F88  //"EXFAT" to HEX

//FAT values
#define EXFAT_FAT_FREE      0x00000000
#define EXFAT_FAT_END       0xFFFFFFFF
#define EXFAT_FAT_BAD       0xFFFFFFF7

//Types of directory entries
#define EXFAT_ENTRY_FILE        0x85  //File
#define EXFAT_ENTRY_DIR         0x85  //Folder (but with attribute)
#define EXFAT_ENTRY_VOLUME      0x83  //Volume Label
#define EXFAT_ENTRY_BITMAP      0x81  //Bitmap
#define EXFAT_ENTRY_UPCASE      0x82  // Up-case table
#define EXFAT_ENTRY_NAME        0xC1  //Continuation of the name
#define EXFAT_ENTRY_STREAM      0xC0  //Data flow

//File attributes
#define EXFAT_ATTR_READ_ONLY    0x01
#define EXFAT_ATTR_HIDDEN       0x02
#define EXFAT_ATTR_SYSTEM       0x04
#define EXFAT_ATTR_VOLUME_ID    0x08
#define EXFAT_ATTR_DIRECTORY    0x10
#define EXFAT_ATTR_ARCHIVE      0x20

//Master Boot Block (VBR)
typedef struct {
    u8  jump_boot[3];
    u8  fs_name[8];              // "EXFAT   "
    u8  reserved[53];
    u64 partition_offset;
    u64 volume_length;
    u32 fat_offset;
    u32 fat_length;
    u32 cluster_heap_offset;
    u32 cluster_count;
    u32 root_dir_cluster;
    u32 volume_serial;
    u16 fs_revision;
    u16 volume_flags;
    u8  bytes_per_sector_shift;
    u8  sectors_per_cluster_shift;
    u8  number_of_fats;
    u8  drive_select;
    u8  percent_in_use;
    u8  reserved2[7];
    u8  boot_code[390];
    u16 signature;                // 0xAA55
} __attribute__((packed)) exfat_vbr_t;
//Write file/directory
typedef struct {
    u8  type;                // 0x85
    u8  secondary_count;
    u16 checksum;
    u16 file_attributes;
    u16 reserved1;
    u32 create_time;
    u32 modify_time;
    u32 access_time;
    u8  create_time_10ms;
    u8  modify_time_10ms;
    u8  create_tz_offset;
    u8  modify_tz_offset;
    u8  access_tz_offset;
    u8  reserved2[7];
} __attribute__((packed)) exfat_file_entry_t;

//Data Stream Recording
typedef struct {
    u8  type;                // 0xC0
    u8  flags;
    u8  reserved1;
    u8  name_length;
    u16 name_hash;
    u16 reserved2;
    u64 valid_data_length;
    u32 reserved3;
    u32 first_cluster;
    u64 data_length;
} __attribute__((packed)) exfat_stream_entry_t;

//Name entry (UTF-16)
typedef struct {
    u8  type;                // 0xC1
    u8  flags;
    u16 name[15];            //15 UTF-16 characters
} __attribute__((packed)) exfat_name_entry_t;

//Bitmap of free clusters
typedef struct {
    u8  type;                // 0x81
    u8  flags;
    u8  reserved[18];
    u32 first_cluster;
    u64 data_length;
} __attribute__((packed)) exfat_bitmap_entry_t;

//exFAT context
typedef struct {
    blockdev_t *dev;
    exfat_vbr_t vbr;
    
    u32 bytes_per_sector;
    u32 sectors_per_cluster;
    u32 bytes_per_cluster;
    
    u64 fat_start;            //FAT offset in bytes
    u64 cluster_heap_start;   //Data offset in bytes
    u32 root_cluster;         //Root cluster
    
    u32 *fat_cache;           //FAT cache
    u32 fat_entries;
} exfat_t;

//Private inode data
typedef struct {
    exfat_t *exfat;
    u32 first_cluster;
    u64 data_length;
    u32 dir_cluster;      //Cluster directory where the entry is located
    u32 dir_entry;        //Record number
    u32 parent_cluster;   //Parent directory cluster
} exfat_inode_private_t;

//exFAT initialization
void exfat_init(void);
int exfat_format(blockdev_t *dev);
int exfat_get_name(vfs_inode_t *inode, char *name, int max_len);

#endif
