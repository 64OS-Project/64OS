#ifndef FAT32_H
#define FAT32_H

#include <kernel/types.h>
#include <fs/vfs.h>

// BPB (BIOS Parameter Block)
typedef struct {
    u8  jump_boot[3];
    u8  oem_name[8];
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  num_fats;
    u16 root_entries;
    u16 total_sectors_16;
    u8  media_descriptor;
    u16 fat_size_16;
    u16 sectors_per_track;
    u16 num_heads;
    u32 hidden_sectors;
    u32 total_sectors_32;
    
    // FAT32 specific
    u32 fat_size_32;
    u16 extended_flags;
    u16 fs_version;
    u32 root_cluster;
    u16 fs_info;
    u16 backup_boot_sector;
    u8  reserved[12];
    u8  drive_number;
    u8  reserved1;
    u8  boot_signature;
    u32 volume_id;
    u8  volume_label[11];
    u8  fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

// FSInfo sector
typedef struct {
    u32 lead_signature;
    u8  reserved1[480];
    u32 struct_signature;
    u32 free_count;
    u32 next_free;
    u8  reserved2[12];
    u32 trail_signature;
} __attribute__((packed)) fat32_fsinfo_t;

// Directory entry
typedef struct {
    u8  name[11];
    u8  attr;
    u8  nt_res;
    u8  crt_time_tenth;
    u16 crt_time;
    u16 crt_date;
    u16 lst_acc_date;
    u16 first_cluster_hi;
    u16 wrt_time;
    u16 wrt_date;
    u16 first_cluster_lo;
    u32 file_size;
} __attribute__((packed)) fat32_dir_entry_t;

// Long filename entry
typedef struct {
    u8  order;
    u16 name1[5];
    u8  attr;
    u8  type;
    u8  checksum;
    u16 name2[6];
    u16 first_cluster;
    u16 name3[2];
} __attribute__((packed)) fat32_lfn_entry_t;

// File attributes
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F

// FAT values
#define FAT32_EOC           0x0FFFFFF8
#define FAT32_BAD           0x0FFFFFF7
#define FAT32_FREE          0x00000000
#define FAT32_RESERVED      0x0FFFFFF0

// Timestamp conversion
#define FAT_DATE(year, month, day)    (((year - 1980) << 9) | (month << 5) | day)
#define FAT_TIME(hour, minute, second) ((hour << 11) | (minute << 5) | (second / 2))

typedef struct {
    blockdev_t *dev;
    fat32_bpb_t bpb;
    fat32_fsinfo_t fsinfo;
    
    u32 bytes_per_sector;
    u32 sectors_per_cluster;
    u32 bytes_per_cluster;
    u32 total_clusters;
    
    u32 fat_start;
    u32 data_start;
    u32 root_cluster;
    u32 fsinfo_sector;
    
    u32 *fat_cache;
    u32 fat_entries;
    
    int dirty;
} fat32;

typedef struct {
    fat32 *fat;
    u32 first_cluster;
    u32 dir_cluster;
    u32 dir_entry;
    u32 parent_cluster;
    u32 entry_offset;  // offset within cluster
} fat32_inode_private_t;

// Public functions
void fat32_init(void);
int fat32_format(blockdev_t *dev);
int fat32_get_name(vfs_inode_t *inode, char *name, int max_len);

#endif
