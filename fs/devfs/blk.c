#include <fs/devfs.h>
#include <kernel/blockdev.h>
#include <mm/heap.h>
#include <kernel/terminal.h>
#include <libk/string.h>

//Wrappers for block devices
static int blk_read_blocks(void *priv, u64 lba, u32 count, void *buf) {
    blockdev_t *dev = (blockdev_t*)priv;
    return blockdev_read(dev, lba, count, buf);
}

static int blk_write_blocks(void *priv, u64 lba, u32 count, const void *buf) {
    blockdev_t *dev = (blockdev_t*)priv;
    return blockdev_write(dev, lba, count, buf);
}

//Driver template
static device_driver_t blk_driver_template = {
    .name = "block",
    .read = NULL,
    .write = NULL,
    .ioctl = NULL,
    .read_blocks = blk_read_blocks,
    .write_blocks = blk_write_blocks
};

//Initializing block devices in the specified directory
void devfs_init_blk(vfs_inode_t *dir) {
    if (!dir) {
        return;
    }
    
    devfs_dir_list_t *list = devfs_get_dir_list(dir);
    if (!list) {
        terminal_error_printf("[DEVFS] init_blk: no list for dir\n");
        return;
    }
    
    blockdev_t *devices[16];
    int count = blockdev_get_list(devices, 16);
    
    for (int i = 0; i < count; i++) {
        //Checking the device
        if (!devices[i]) {
            terminal_error_printf("[DEVFS] devfs_init_blk: device %d is NULL!\n", i);
            continue;
        }
        
        //Creating a driver
        device_driver_t *drv = (device_driver_t*)malloc(sizeof(device_driver_t));
        if (!drv) {
            terminal_error_printf("[DEVFS] devfs_init_blk: failed to allocate driver for %s\n", 
                       devices[i]->name);
            continue;
        }
        
        memset(drv, 0, sizeof(device_driver_t));
        strcpy(drv->name, "block");
        drv->read_blocks = blk_read_blocks;
        drv->write_blocks = blk_write_blocks;
        
        //Create a node name
        char node_name[32];
        strncpy(node_name, devices[i]->name, sizeof(node_name) - 1);
        node_name[sizeof(node_name) - 1] = '\0';
        
        //Create a node
        int ret = devfs_mknod_in(dir, node_name, FT_BLKDEV, drv, devices[i]);
    }

    if (list) {
        terminal_printf("[DEVFS] devfs_init_blk: final count in dir = %d\n", list->count);
    }
}
