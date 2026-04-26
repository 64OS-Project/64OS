#include <fs/devfs.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>

vfs_inode_t *devfs_root = NULL;
static device_driver_t *driver_list = NULL;
extern vfs_inode_t *root_inode;

//VFS Operations for Devices
static int devfs_lookup(vfs_inode_t *dir, const char *name, vfs_inode_t **result);
static int devfs_readdir(vfs_inode_t *dir, u64 *pos, char *name, u32 *name_len, u32 *type);
static int devfs_get_name(vfs_inode_t *inode, char *name, int max_len);
static int devfs_parent(vfs_inode_t *inode, vfs_inode_t **parent);

static devfs_dir_list_t *get_dir_list(vfs_inode_t *dir) {
    if (!dir) return NULL;
    devfs_node_t *node = (devfs_node_t*)dir->i_private;
    if (!node || node->type != FT_DIR) return NULL;
    return (devfs_dir_list_t*)node->private;
}

//Get a name
static int devfs_get_name(vfs_inode_t *inode, char *name, int max_len) {
    if (!inode || !name) return -1;
    devfs_node_t *node = (devfs_node_t*)inode->i_private;
    if (!node) return -1;
    strncpy(name, node->name, max_len - 1);
    name[max_len - 1] = '\0';
    return 0;
}

static int devfs_parent(vfs_inode_t *inode, vfs_inode_t **parent) {
    if (!inode || !parent) return -1;
    
    devfs_node_t *node = (devfs_node_t*)inode->i_private;
    if (!node) return -1;
    
    if (node->parent_inode) {
        *parent = node->parent_inode;
        return 0;
    }
    *parent = root_inode;
    return 0;
}

//VFS Operations
static vfs_operations_t devfs_i_op = {
    .lookup = devfs_lookup,
    .readdir = devfs_readdir,
    .get_name = devfs_get_name,
    .parent = devfs_parent,
};

static vfs_file_operations_t devfs_f_op = {0};

//Creating a directory
vfs_inode_t *devfs_create_dir(const char *name) {
    vfs_inode_t *dir = vfs_alloc_inode();
    if (!dir) return NULL;
    
    devfs_node_t *node = (devfs_node_t*)malloc(sizeof(devfs_node_t));
    if (!node) { vfs_free_inode(dir); return NULL; }
    memset(node, 0, sizeof(devfs_node_t));
    
    if (name) strncpy(node->name, name, 31);
    node->type = FT_DIR;
    node->inode = dir;
    
    devfs_dir_list_t *list = (devfs_dir_list_t*)malloc(sizeof(devfs_dir_list_t));
    if (!list) { free(node); vfs_free_inode(dir); return NULL; }
    memset(list, 0, sizeof(devfs_dir_list_t));
    
    node->private = list;
    dir->i_private = node;
    dir->i_mode = FT_DIR;
    dir->i_op = &devfs_i_op;
    dir->i_fop = &devfs_f_op;
    
    return dir;
}

//Add an item to a directory
static void devfs_add_to_dir(vfs_inode_t *dir, const char *name, int type, 
                             device_driver_t *drv, void *private) {
    if (!dir || !name) return;
    
    devfs_node_t *parent_node = (devfs_node_t*)dir->i_private;
    if (!parent_node || parent_node->type != FT_DIR) return;
    
    devfs_dir_list_t *list = (devfs_dir_list_t*)parent_node->private;
    if (!list) return;
    
    devfs_node_t *entry = (devfs_node_t*)malloc(sizeof(devfs_node_t));
    if (!entry) return;
    memset(entry, 0, sizeof(devfs_node_t));
    
    strncpy(entry->name, name, 31);
    entry->type = type;
    entry->driver = drv;
    entry->private = private;
    entry->parent = parent_node;
    entry->parent_inode = dir;  //<-- SAVE THE PARENTAL INOD!
    
    if (type == FT_DIR && private) {
        entry->inode = (vfs_inode_t*)private;
        devfs_node_t *subdir_node = (devfs_node_t*)((vfs_inode_t*)private)->i_private;
    	if (subdir_node) {
            subdir_node->parent_inode = dir;  //parent inode
    }
    }
    
    if (list->last) {
        list->last->next = entry;
        list->last = entry;
    } else {
        list->first = entry;
        list->last = entry;
    }
    list->count++;
}

//lookup operation
static int devfs_lookup(vfs_inode_t *dir, const char *name, vfs_inode_t **result) {
    if (!dir || !name || !result) return -1;
    
    devfs_node_t *parent_node = (devfs_node_t*)dir->i_private;
    if (!parent_node || parent_node->type != FT_DIR) return -1;
    
    devfs_dir_list_t *list = (devfs_dir_list_t*)parent_node->private;
    if (!list) return -1;
    
    for (devfs_node_t *entry = list->first; entry; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) {
            if (entry->inode) {
                *result = entry->inode;
                return 0;
            }
            
            vfs_inode_t *inode = vfs_alloc_inode();
            if (!inode) return -1;
            
            inode->i_mode = entry->type;
            inode->i_op = &devfs_i_op;
            inode->i_fop = &devfs_f_op;
            inode->i_private = entry;
            
            entry->inode = inode;
            *result = inode;
            return 0;
        }
    }
    return -1;
}

//readdir operation
static int devfs_readdir(vfs_inode_t *dir, u64 *pos, char *name, 
                         u32 *name_len, u32 *type) {
    if (!dir || !pos || !name || !name_len || !type) return -1;
    
    devfs_dir_list_t *list = get_dir_list(dir);
    if (!list) return -1;
    
    if (*pos >= (u64)list->count) return -1;
    
    int idx = 0;
    for (devfs_node_t *entry = list->first; entry; entry = entry->next) {
        if (idx == *pos) {
            strcpy(name, entry->name);
            *name_len = strlen(entry->name);
            *type = entry->type;
            (*pos)++;
            return 0;
        }
        idx++;
    }
    return -1;
}

//Driver registration
int devfs_register_driver(device_driver_t *drv) {
    drv->next = driver_list;
    driver_list = drv;
    return 0;
}

//Creating a node
int devfs_mknod_in(vfs_inode_t *dir, const char *name, int type, 
                   device_driver_t *drv, void *private) {
    devfs_add_to_dir(dir, name, type, drv, private);
    return 0;
}

//Read from device
static int devfs_read(vfs_inode_t *inode, u64 offset, void *buf, 
                      u32 size, u32 *read) {
    if (!inode || !buf || !read) return -1;
    
    devfs_node_t *entry = (devfs_node_t*)inode->i_private;
    if (!entry || !entry->driver) {
        terminal_error_printf("[DEVFS] read: no driver for inode %p\n", inode);
        return -1;
    }
    
    if (entry->type == FT_CHRDEV) {
        if (!entry->driver->read) return -1;
        return entry->driver->read(buf, size, (sz*)read);      
    } else if (entry->type == FT_BLKDEV) {
        if (!entry->driver->read_blocks) return -1;
        
        //Convert offset to LBA
        u64 lba = offset / 512;
        u32 count = (size + 511) / 512;
        
        int ret = entry->driver->read_blocks(entry->private, lba, count, buf);
        if (ret == 0) *read = size;
        return ret;
    }
    
    return -1;
}

//Write to device
static int devfs_write(vfs_inode_t *inode, u64 offset, const void *buf, 
                       u32 size, u32 *written) {
    if (!inode || !buf || !written) return -1;
    
    devfs_node_t *entry = (devfs_node_t*)inode->i_private;
    if (!entry || !entry->driver) return -1;
    
    if (entry->type == FT_CHRDEV) {
        if (!entry->driver->write) return -1;
        return entry->driver->write(buf, size, (sz*)written);
    } else if (entry->type == FT_BLKDEV) {
        if (!entry->driver->write_blocks) return -1;
        
        u64 lba = offset / 512;
        u32 count = (size + 511) / 512;
        
        int ret = entry->driver->write_blocks(entry->private, lba, count, buf);
        if (ret == 0) *written = size;
        return ret;
    }
    
    return -1;
}

//Initializing devfs
void devfs_init(void) {
    devfs_root = devfs_create_dir("dev");
    if (!devfs_root) {
        terminal_error_printf("[DEVFS] FATAL: Failed to create /dev\n");
        return;
    }
    
    //Mount /dev in VFS
    vfs_mount_point("/dev", devfs_root);
    
    //Create /dev/blk
    vfs_inode_t *blk_dir = devfs_create_dir("blk");
    if (!blk_dir) {
        terminal_printf("[DEVFS] FATAL: Failed to create /dev/blk\n");
        return;
    }
    
    //Add blk to the root directory /dev
    devfs_add_to_dir(devfs_root, "blk", FT_DIR, NULL, blk_dir);

    terminal_printf("[DEVFS] FATAL: blk_dir created at %p\n", blk_dir);
    
    //Initialize block devices in /dev/blk
    terminal_printf("[DEVFS] Calling devfs_init_blk...\n");
    devfs_init_blk(blk_dir);
    
    //Checking the contents of /dev/blk (using the correct function!)
    devfs_dir_list_t *blk_list = devfs_get_dir_list(blk_dir);
    if (blk_list) {
        terminal_printf("[DEVFS] /dev/blk contains %d entries:\n", blk_list->count);
        devfs_node_t *entry = blk_list->first;
        int i = 0;
        while (entry) {
            terminal_printf("[DEVFS]   entry[%d]: '%s' (type=%d)\n", 
                       i++, entry->name, entry->type);
            entry = entry->next;
        }
    }
    
    //Create /std
    vfs_inode_t *std_root = devfs_create_dir("std");
    if (std_root) {
        vfs_mount_point("/std", std_root);
        devfs_init_std(std_root);
    }
}
