#ifndef DEVFS_H
#define DEVFS_H

#include <kernel/types.h>
#include <fs/vfs.h>
#include <kernel/terminal.h>

//Device Driver Structure
typedef struct device_driver {
    char name[32];
    
    //For character devices (stdin/stdout)
    int (*read)(void *buf, sz count, sz *read);
    int (*write)(const void *buf, sz count, sz *written);
    int (*ioctl)(u32 cmd, void *arg);
    
    //For block devices - corrected signatures
    int (*read_blocks)(void *priv, u64 lba, u32 count, void *buf);
    int (*write_blocks)(void *priv, u64 lba, u32 count, const void *buf);
    
    struct device_driver *next;
} device_driver_t;

//Structure for representing a device in VFS
typedef struct devfs_node {
    char name[32];
    int type;  //FT_CHRDEV or FT_BLKDEV
    device_driver_t *driver;
    void *private;
    struct devfs_node *next;
    struct devfs_node *parent;
    vfs_inode_t *inode;
    vfs_inode_t *parent_inode;
} devfs_node_t;

//Each directory has its own list
typedef struct {
    devfs_node_t *first;
    devfs_node_t *last;
    int count;  //Element counter for debugging
} devfs_dir_list_t;

//Initializing /dev
void devfs_init(void);

vfs_inode_t *devfs_create_dir(const char *name);

//Registering a Device Driver
int devfs_register_driver(device_driver_t *drv);

//Create a device node
int devfs_mknod(const char *name, int type, device_driver_t *drv, void *private);

int devfs_mknod_in(vfs_inode_t *dir, const char *name, int type, device_driver_t *drv, void *private);

//Get device inode
vfs_inode_t *devfs_get_inode(const char *path);

void devfs_init_std(vfs_inode_t *dir);
void devfs_init_blk(vfs_inode_t *dir);

//Global variable for the devfs root (needed for comparison in std.c)
extern vfs_inode_t *devfs_root;

//So that other files can get the list
static inline devfs_dir_list_t *devfs_get_dir_list(vfs_inode_t *dir) {
    if (!dir || dir->i_mode != FT_DIR) return NULL;
    devfs_node_t *node = (devfs_node_t*)dir->i_private;
    if (!node || node->type != FT_DIR) return NULL;
    return (devfs_dir_list_t*)node->private;
}

#endif
