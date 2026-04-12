#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include <kernel/types.h>

/*
 * Maximum number of disks in the system
 */
#define MAX_BLOCK_DEVS 64
#define BLOCKDEV_NAME_LEN 32

/*
 * Types of block devices
 */
typedef enum {
    BLOCKDEV_TYPE_NONE = 0,
    BLOCKDEV_TYPE_IDE,
    BLOCKDEV_TYPE_AHCI,
    BLOCKDEV_TYPE_RAMDISK,
    BLOCKDEV_TYPE_NVME
} blockdev_type_t;

/*
 * Device status
 */
typedef enum {
    BLOCKDEV_UNINITIALIZED = 0,
    BLOCKDEV_READY,
    BLOCKDEV_ERROR,
    BLOCKDEV_NO_MEDIA
} blockdev_status_t;

/*
 * Basic structure of a block device
 */
typedef struct blockdev {
    /*
 * Identification
 */
    char name[BLOCKDEV_NAME_LEN];
    blockdev_type_t type;
    blockdev_status_t status;
    
    /*
 * Geometry
 */
    u64 sector_size;      /*
 * Sector size in bytes (usually 512)
 */
    u64 total_sectors;    /*
 * Total number of sectors
 */
    u64 total_bytes;      /*
 * Total size in bytes
 */
    bool supports_lba48;       /*
 * LBA48 support
 */
    
    /*
 * Handler functions (similar to VFS)
 */
    int (*read_sectors)(struct blockdev *dev, u64 lba, 
                        u32 count, void *buffer);
    int (*write_sectors)(struct blockdev *dev, u64 lba, 
                         u32 count, const void *buffer);
    int (*flush_cache)(struct blockdev *dev);
    
    /*
 * Statistics
 */
    u64 read_count;
    u64 write_count;
    u64 error_count;
    
    /*
 * Specific data for different device types
 */
    union {
        struct {
            void *ide_disk;    /*
 * Pointer to ide_disk_t
 */
            u8 channel;   /*
 * IDE_CHANNEL_PRIMARY/SECONDARY
 */
            u8 drive;     /*
 * 0=master, 1=slave
 */
        } ide;

        struct {
            void *ahci_port;    //<-- pointer to ahci_port_t
            int   port_num;     //<-- port number
        } ahci;
        
        struct {
            void *ramdisk_data;
            sz ramdisk_size;
        } ramdisk;
        
    } device_data;
    
    /*
 * Link for list
 */
    struct blockdev *next;
} blockdev_t;

/*
 * ==================== API ====================
 */

/*
 * Initializing the block device system
 */
void blockdev_init(void);

/*
 * Registering a new device
 */
blockdev_t* blockdev_register(const char *name, blockdev_type_t type);

/*
 * Special enrollment features for specific device types
 */
void blockdev_register_ide(void *ide_disk, const char *name, 
                          u8 channel, u8 drive);
int nvme_read_wrapper(blockdev_t *bdev, u64 lba, u32 count, void *buffer);
int nvme_write_wrapper(blockdev_t *bdev, u64 lba, u32 count, const void *buffer);
void blockdev_scan_all_disks(int type);

/*
 * Search for a device by name
 */
blockdev_t* blockdev_find(const char *name);

/*
 * Search for a device by number
 */
blockdev_t* blockdev_find_by_number(int num);

/*
 * Getting a list of all devices
 */
int blockdev_get_list(blockdev_t **list, int max_count);

/*
 * Read sectors (universal function)
 */
int blockdev_read(blockdev_t *dev, u64 lba, 
                  u32 count, void *buffer);

/*
 * Write sectors (universal function)
 */
int blockdev_write(blockdev_t *dev, u64 lba, 
                   u32 count, const void *buffer);

/*
 * Resetting the device cache
 */
int blockdev_flush(blockdev_t *dev);

/*
 * Getting device information
 */
void blockdev_get_info(blockdev_t *dev, char *buffer, sz buf_size);

/*
 * Removing a device (when removed)
 */
void blockdev_unregister(blockdev_t *dev);

/*
 * Debug output of all devices
 */
void blockdev_dump_all(void);

/*
 * Disk utilities
 */
static inline u64 blockdev_get_size(blockdev_t *dev) {
    return dev ? dev->total_bytes : 0;
}

static inline u64 blockdev_get_sector_size(blockdev_t *dev) {
    return dev ? dev->sector_size : 0;
}

static inline const char* blockdev_get_name(blockdev_t *dev) {
    return dev ? dev->name : "unknown";
}

#endif
