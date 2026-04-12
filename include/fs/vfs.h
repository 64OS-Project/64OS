#ifndef VFS_H
#define VFS_H

#include <kernel/types.h>
#include <kernel/blockdev.h>

//Forward declarations
struct vfs_inode;
struct vfs_file;

//File types
#define FT_UNKNOWN   0
#define FT_REG_FILE  1
#define FT_DIR       2
#define FT_CHRDEV    3
#define FT_BLKDEV    4
#define FT_FIFO      5
#define FT_SOCK      6
#define FT_SYMLINK   7

//Opening flags
#define O_READ       1
#define O_WRITE      2
#define O_CREAT      4
#define O_TRUNC      8
#define O_APPEND     16

//Maximum path length
#define PATH_MAX     4096
#define NAME_MAX     255

//Operations with inodes (metadata)
typedef struct vfs_operations {
    int (*lookup)(struct vfs_inode *dir, const char *name, struct vfs_inode **result);
    int (*create)(struct vfs_inode *dir, const char *name, u32 mode, struct vfs_inode **result);
    int (*unlink)(struct vfs_inode *dir, const char *name);
    int (*mkdir)(struct vfs_inode *dir, const char *name, u32 mode, struct vfs_inode **result);
    int (*rmdir)(struct vfs_inode *dir, const char *name);
    int (*rename)(struct vfs_inode *old_dir, const char *old_name, 
                  struct vfs_inode *new_dir, const char *new_name);
    int (*chmod)(struct vfs_inode *inode, u32 mode);
    int (*stat)(struct vfs_inode *inode, void *stat_buf);
    int (*readdir)(struct vfs_inode *dir, u64 *pos, char *name, 
                   u32 *name_len, u32 *type);
    int (*parent)(struct vfs_inode *inode, struct vfs_inode **parent);
    int (*get_name)(struct vfs_inode *inode, char *name, int max_len);
    int (*unmount)(struct vfs_inode *root);  //
} vfs_operations_t;

//Operations with open files (data)
typedef struct vfs_file_operations {
    int (*read)(struct vfs_inode *inode, u64 offset, void *buf, u32 size, u32 *read);
    int (*write)(struct vfs_inode *inode, u64 offset, const void *buf, u32 size, u32 *written);
    int (*truncate)(struct vfs_inode *inode, u64 new_size);
    int (*sync)(struct vfs_inode *inode);
} vfs_file_operations_t;

//Inode (represents a file or directory)
typedef struct vfs_inode {
    u32 i_mode;        //Type and rights
    u32 i_uid;
    u32 i_gid;
    u64 i_size;
    u64 i_ctime;       //Creation time
    u64 i_mtime;       //Change time
    u64 i_atime;       //Access time
    u64 i_ino;         //Unique number

    blockdev_t *i_dev;      //The device on which the inode is located
    char i_fs_name[16];     //File system name ("exfat", "fat32")
    
    //Links
    u32 i_nlink;       //Number of hard links
    
    //Operations
    vfs_operations_t *i_op;
    vfs_file_operations_t *i_fop;

    u32 i_refcount;
    
    //Private data of a specific FS
    void *i_private;
    
    //For caching
    int i_dirty;
    struct vfs_inode *i_next;  //For list
} vfs_inode_t;

//Open file (file descriptor)
typedef struct vfs_file {
    vfs_inode_t *f_inode;
    u64 f_pos;
    u32 f_flags;
    void *f_private;        //For a specific FS
} vfs_file_t;

//Structure for registering a file system
typedef struct file_system {
    char name[16];
    int (*mount)(blockdev_t *dev, struct vfs_inode **root);
    int (*unmount)(struct vfs_inode *root);
    struct file_system *next;
} file_system_t;

//File statistics (for POSIX compatibility)
typedef struct vfs_stat {
    u32 st_mode;
    u32 st_uid;
    u32 st_gid;
    u64 st_size;
    u64 st_ctime;
    u64 st_mtime;
    u64 st_atime;
    u64 st_ino;
    u32 st_nlink;
} vfs_stat_t;


typedef struct {
    char name[256];
    vfs_inode_t *inode;
    int type;
} mount_entry_t;

extern vfs_inode_t *devfs_root;
extern vfs_inode_t *current_dir;

//==================== KERNEL API ====================

//VFS Initialization
void vfs_init(void);

//File system registration
int vfs_register_fs(file_system_t *fs);

//Mounting
int vfs_mount(const char *fs_name, blockdev_t *dev, vfs_inode_t **root);
int vfs_unmount(vfs_inode_t *root);

//Path Operations
int vfs_walk(vfs_inode_t *dir, const char *path, vfs_inode_t **result);
int vfs_walk_parent(vfs_inode_t *dir, const char *path, vfs_inode_t **parent, char *name);
int vfs_lookup(vfs_inode_t *dir, const char *name, vfs_inode_t **result);

//Opening/closing files
int vfs_open(vfs_inode_t *dir, const char *path, u32 flags, vfs_file_t **file);
int vfs_close(vfs_file_t *file);

//Read/Write
int vfs_read(vfs_file_t *file, void *buf, u32 size, u32 *read);
int vfs_write(vfs_file_t *file, const void *buf, u32 size, u32 *written);
int vfs_seek(vfs_file_t *file, u64 offset, int whence);

//Directory Operations
int vfs_mkdir(vfs_inode_t *dir, const char *name, u32 mode, vfs_inode_t **result);
int vfs_rmdir(vfs_inode_t *dir, const char *name);
int vfs_readdir(vfs_inode_t *dir, u64 *pos, char *name, u32 *name_len, u32 *type);

//File management
int vfs_create(vfs_inode_t *dir, const char *name, u32 mode, vfs_inode_t **result);
int vfs_unlink(vfs_inode_t *dir, const char *name);
int vfs_rename(vfs_inode_t *old_dir, const char *old_name, 
               vfs_inode_t *new_dir, const char *new_name);

//Metadata
int vfs_stat(vfs_inode_t *inode, vfs_stat_t *stat);
int vfs_chmod(vfs_inode_t *inode, u32 mode);
int vfs_sync(vfs_inode_t *inode);

//Inode management (for FS)
vfs_inode_t *vfs_alloc_inode(void);
void vfs_free_inode(vfs_inode_t *inode);

int vfs_parent(vfs_inode_t *inode, vfs_inode_t **parent);

//Create a mount point
int vfs_mount_point(const char *path, vfs_inode_t *inode);

int build_current_path(vfs_inode_t *dir, const char *component, char *out);

int vfs_get_mount_points(const char *path, mount_entry_t *entries, int max_entries);

void vfs_inode_unref(vfs_inode_t *inode);
void vfs_inode_ref(vfs_inode_t *inode);

int vfs_build_path(vfs_inode_t *inode, char *buffer, int max_len);
int vfs_get_path_depth(vfs_inode_t *inode);

int vfs_chown(vfs_inode_t *inode, u32 uid, u32 gid);

#endif
