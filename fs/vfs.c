#include <fs/vfs.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <fs/fat32.h>
#include <fs/exfat.h>

typedef struct mount_point {
    char path[PATH_MAX];
    vfs_inode_t *inode;
    struct mount_point *next;
} mount_point_t;

//Global Variables
static file_system_t *fs_list = NULL;
vfs_inode_t *root_inode = NULL;
static mount_point_t *mounts = NULL;
extern vfs_inode_t *fs_root;
vfs_inode_t *current_dir = NULL;
char current_path[PATH_MAX] = "/";

//The simplest inode allocator (in reality caching is needed)
static u64 next_ino = 1;

static int vfs_resolve_path(vfs_inode_t *base_dir, const char *path, 
                            vfs_inode_t **parent, char *name, bool follow_last) {
    if (!path || !parent || !name) return -1;
    
    //Empty path or root
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        *parent = base_dir ? base_dir : fs_root;
        vfs_inode_ref(*parent);
        strcpy(name, "");
        return 0;
    }
    
    //Absolute path
    if (path[0] == '/') {
        //Finding the last component
        const char *last_slash = strrchr(path, '/');
        
        if (last_slash == path) {
            //"/name" - parent root
            *parent = fs_root;
            vfs_inode_ref(*parent);
            strcpy(name, path + 1);
            return 0;
        }
        
        //Copy the path up to the last slash
        char parent_path[PATH_MAX];
        sz parent_len = last_slash - path;
        if (parent_len >= PATH_MAX) return -1;
        
        strncpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';
        
        //Getting the parent directory
        if (vfs_walk(fs_root, parent_path, parent) != 0) {
            return -1;
        }
        
        strcpy(name, last_slash + 1);
        return 0;
    }
    
    //Relative path - use base_dir
    if (!base_dir) return -1;
    
    //Looking for the last component
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        //Just name - parent = base_dir
        *parent = base_dir;
        vfs_inode_ref(*parent);
        strcpy(name, path);
        return 0;
    }
    
    //Complex relative path "dir1/dir2/name"
    char parent_relative[PATH_MAX];
    sz parent_len = last_slash - path;
    if (parent_len >= PATH_MAX) return -1;
    
    strncpy(parent_relative, path, parent_len);
    parent_relative[parent_len] = '\0';
    
    //Getting the parent directory
    if (vfs_walk(base_dir, parent_relative, parent) != 0) {
        return -1;
    }
    
    strcpy(name, last_slash + 1);
    return 0;
}

static int get_inode_name(vfs_inode_t *inode, char *name, int max_len) {
    if (!inode || !name) return -1;
    
    //For the root
    if (inode == fs_root) {
        strcpy(name, "");
        return 0;
    }
    
    //Trying to get a name via VFS
    if (inode->i_op && inode->i_op->get_name) {
        return inode->i_op->get_name(inode, name, max_len);
    }
    
    return -1;
}

//Add for debugging (optional)
int vfs_get_path_depth(vfs_inode_t *inode) {
    int depth = 0;
    vfs_inode_t *current = inode;
    vfs_inode_t *parent = NULL;
    
    while (current && current != root_inode && depth < 1000) {
        if (vfs_parent(current, &parent) != 0) break;
        if (parent == current) break;
        current = parent;
        depth++;
    }
    
    return depth;
}

//New function: collects path from inode without recursion
int vfs_build_path(vfs_inode_t *inode, char *buffer, int max_len) {
    if (!inode || !buffer || max_len <= 0) return -1;
    
    //Root
    if (inode == root_inode) {
        strcpy(buffer, "/");
        return 0;
    }
    
    //Assembling path components
    char *components[256];
    int comp_count = 0;
    vfs_inode_t *current = inode;
    vfs_inode_t *parent = NULL;
    
    while (current && current != root_inode && comp_count < 256) {
        char name[NAME_MAX];
        name[0] = '\0';
        
        if (current->i_op && current->i_op->get_name) {
            int ret = current->i_op->get_name(current, name, sizeof(name));
            if (current->i_op->get_name(current, name, sizeof(name)) == 0 && name[0] != '\0') {
                components[comp_count] = malloc(strlen(name) + 1);
                if (!components[comp_count]) {
                    //Cleanup on error
                    for (int j = 0; j < comp_count; j++) free(components[j]);
                    return -1;
                }
                strcpy(components[comp_count], name);
                comp_count++;
            }
        }
        
        //Getting the parent
        if (vfs_parent(current, &parent) != 0) break;
        if (parent == current) break;  //Loop protection
        
        current = parent;
    }
    
    //Putting the path together backwards
    buffer[0] = '/';
    int pos = 1;
    
    for (int i = comp_count - 1; i >= 0; i--) {
        int name_len = strlen(components[i]);
        
        if (pos + name_len + 1 >= max_len) {
            //Buffer full
            for (int j = 0; j < comp_count; j++) free(components[j]);
            return -1;
        }
        
        strcpy(buffer + pos, components[i]);
        pos += name_len;
        
        if (i > 0) {
            buffer[pos++] = '/';
        }
        
        free(components[i]);
    }
    
    buffer[pos] = '\0';
    return 0;
}

//Wrapper for compatibility with old code
char* build_path_recursive(vfs_inode_t *inode, char *buffer, int depth) {
    (void)depth;  //Not used
    if (vfs_build_path(inode, buffer, PATH_MAX) == 0) {
        return buffer;
    }
    strcpy(buffer, "/");
    return buffer;
}

//Update current_path
void update_current_path(void) {
    if (!root_inode || !current_dir) {
        strcpy(current_path, "/");
        return;
    }
    
    if (vfs_build_path(current_dir, current_path, PATH_MAX) != 0) {
        strcpy(current_path, "/");
    }
}

void vfs_init(void) {
    fs_list = NULL;
    root_inode = NULL;
    next_ino = 1;

    
    terminal_printf("[VFS] Initialized\n");
}

int vfs_mount_point(const char *path, vfs_inode_t *inode) {
    mount_point_t *mp = (mount_point_t*)malloc(sizeof(mount_point_t));
    if (!mp) return -1;  //<-- THIS IS IMPORTANT!
    
    strcpy(mp->path, path);
    mp->inode = inode;
    mp->next = mounts;
    mounts = mp;
    return 0;
}

int build_current_path(vfs_inode_t *dir, const char *component, char *out) {
    if (!dir || !out) return -1;
    
    char base_path[PATH_MAX];
    
    //Building the path to the current directory
    if (vfs_build_path(dir, base_path, PATH_MAX) != 0) {
        return -1;
    }
    
    //If the component is not specified, return the directory path
    if (!component || component[0] == '\0') {
        strcpy(out, base_path);
        return 0;
    }
    
    //Adding a component to the path
    int base_len = strlen(base_path);
    int comp_len = strlen(component);
    
    if (base_len + comp_len + 2 > PATH_MAX) {
        return -1;
    }
    
    if (base_len > 0 && base_path[base_len - 1] == '/') {
        snprintf(out, PATH_MAX, "%s%s", base_path, component);
    } else {
        snprintf(out, PATH_MAX, "%s/%s", base_path, component);
    }
    
    return 0;
}

static int get_parent_inode(vfs_inode_t *inode, vfs_inode_t **parent) {
    if (!inode || !parent) return -1;
    
    //If this is the VFS root
    if (inode == root_inode) {
        *parent = root_inode;
        vfs_inode_ref(*parent);
        return 0;
    }
    
    //Checking if this is a mount point
    mount_point_t *mp = mounts;
    while (mp) {
        if (mp->inode == inode) {
            //Found the mount point - need to go higher
            vfs_inode_t *real_parent = NULL;
            if (vfs_parent(mp->inode, &real_parent) == 0) {
                *parent = real_parent;
                return 0;
            }
            break;
        }
        mp = mp->next;
    }
    
    //We use the parent operation from the FS
    if (inode->i_op && inode->i_op->parent) {
        return inode->i_op->parent(inode, parent);
    }
    
    //If all else fails, the parent is root
    *parent = root_inode;
    vfs_inode_ref(*parent);
    return 0;
}

//Add to the beginning of the file (if not present)
static vfs_inode_t *check_mount_points(const char *path) {
    mount_point_t *mp = mounts;
    while (mp) {
        if (strcmp(mp->path, path) == 0) {
            return mp->inode;
        }
        mp = mp->next;
    }
    return NULL;
}

int vfs_register_fs(file_system_t *fs) {
    if (!fs) return -1;
    
    fs->next = fs_list;
    fs_list = fs;
    return 0;
}

static const char* detect_filesystem(blockdev_t *dev) {
    u8 sector[512];
    
    //Reading the first sector
    if (blockdev_read(dev, 0, 1, sector) != 0)
        return NULL;
    
    //FAT32 check
    fat32_bpb_t *bpb = (fat32_bpb_t*)sector;
    if (bpb->bytes_per_sector == 512 && 
        bpb->sectors_per_cluster > 0 &&
        bpb->fat_size_32 > 0 &&
        sector[510] == 0x55 && sector[511] == 0xAA) {
        
        if (bpb->fs_type[0] == 'F' && memcmp(bpb->fs_type, "FAT32", 5) == 0)
            return "fat32";
        if (bpb->fs_type[0] == 'F' && memcmp(bpb->fs_type, "FAT16", 5) == 0)
            return "fat32";  //FAT16 is also supported via fat32 driver
    }
    
    //exFAT check
    exfat_vbr_t *exfat = (exfat_vbr_t*)sector;
    if (memcmp(exfat->fs_name, "EXFAT   ", 8) == 0 &&
        sector[510] == 0x55 && sector[511] == 0xAA) {
        return "exfat";
    }
    
    return NULL;
}

int vfs_mount(const char *fs_name, blockdev_t *dev, vfs_inode_t **root) {
    if (!dev || !root) return -1;

    char detected_fs[16] = {0};
    const char *real_fs_name = fs_name;

    if (!fs_name || fs_name[0] == '\0') {
        const char *detected = detect_filesystem(dev);
        if (!detected) {
            terminal_error_printf("[VFS] Cannot detect filesystem on %s\n", dev->name);
            return -1;
        }
        strcpy(detected_fs, detected);
        real_fs_name = detected_fs;
        terminal_printf("[VFS] Detected %s on %s\n", detected, dev->name);
    }
    
    file_system_t *fs = fs_list;
    while (fs) {
        if (strcmp(fs->name, fs_name) == 0) {
            int ret = fs->mount(dev, root);
            if (ret == 0) {
                root_inode = *root;
                vfs_inode_ref(root_inode);
                terminal_printf("[VFS] Mounted %s\n", fs_name);
            }
            return ret;
        }
        fs = fs->next;
    }
    
    terminal_error_printf("[VFS] Filesystem %s not found\n", fs_name);
    return -1;
}

int vfs_unmount(vfs_inode_t *root) {
    if (!root) return -1;
    
    //Getting a file system from private data
    //For exFAT: exfat_inode_private_t *priv = (exfat_inode_private_t*)root->i_private;
    //For FAT32: fat32_inode_private_t *priv = (fat32_inode_private_t*)root->i_private;
    
    //Call the FS-specific unmount function
    if (root->i_op && root->i_op->unmount) {
        root->i_op->unmount(root);
    }
    
    //Synchronizing all dirty data
    vfs_sync(root);
    
    //Freeing the root inode
    vfs_inode_unref(root);
    
    root_inode = NULL;
    
    terminal_printf("[VFS] Unmounted filesystem\n");
    return 0;
}

//Parsing the path into components
static int split_path(const char *path, char *name, const char **rest) {
    if (!path || path[0] == '\0') return -1;
    
    //Skip leading slashes
    while (*path == '/') path++;
    if (*path == '\0') return -1;
    
    const char *end = path;
    while (*end && *end != '/') end++;
    
    int len = end - path;
    if (len >= NAME_MAX) len = NAME_MAX - 1;
    
    memcpy(name, path, len);
    name[len] = '\0';
    
    *rest = *end ? end : NULL;
    return 0;
}

//Path Traversal with Mount Point Support
int vfs_walk(vfs_inode_t *dir, const char *path, vfs_inode_t **result) {
    if (!dir || !path || !result) return -1;
    
    //Empty path or root
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        vfs_inode_t *mount_point = check_mount_points("/");
        *result = mount_point ? mount_point : root_inode;
        vfs_inode_ref(*result);
        return 0;
    }
    
    vfs_inode_t *current = dir;
    vfs_inode_ref(current);
    
    char *saveptr;
    //Copy the path for parsing
    char path_copy[PATH_MAX];
    strncpy(path_copy, path, PATH_MAX - 1);
    path_copy[PATH_MAX - 1] = '\0';
    
    //Breaking it down into components
    char *components[256];
    int comp_count = 0;
    char *token = strtok_r(path_copy, "/", &saveptr);
    
    while (token && comp_count < 256) {
        components[comp_count++] = token;
        token = strtok_r(NULL, "/", &saveptr);
    }
    
    //Let's go by components
    for (int i = 0; i < comp_count; i++) {
        char *comp = components[i];
        
        //We skip "."
        if (strcmp(comp, ".") == 0) {
            continue;
        }
        
        //Processing ".."
        if (strcmp(comp, "..") == 0) {
            vfs_inode_t *parent = NULL;
            if (get_parent_inode(current, &parent) != 0) {
                vfs_inode_unref(current);
                return -1;
            }
            vfs_inode_unref(current);
            current = parent;
            continue;
        }
        
        //Checking the mount point
        char current_path_buf[PATH_MAX];
        if (current == root_inode) {
            snprintf(current_path_buf, PATH_MAX, "/%s", comp);
        } else if (vfs_build_path(current, current_path_buf, PATH_MAX) == 0) {
            if (current_path_buf[strlen(current_path_buf) - 1] != '/') {
                strcat(current_path_buf, "/");
            }
            strcat(current_path_buf, comp);
        } else {
            current_path_buf[0] = '\0';
        }
        
        if (current_path_buf[0] != '\0') {
            vfs_inode_t *mount_point = check_mount_points(current_path_buf);
            if (mount_point) {
                vfs_inode_unref(current);
                current = mount_point;
                vfs_inode_ref(current);
                continue;
            }
        }
        
        //Regular lookup
        if (!current->i_op || !current->i_op->lookup) {
            vfs_inode_unref(current);
            return -1;
        }
        
        vfs_inode_t *next = NULL;
        if (current->i_op->lookup(current, comp, &next) != 0) {
            vfs_inode_unref(current);
            return -1;
        }
        
        vfs_inode_unref(current);
        current = next;
    }
    
    *result = current;
    return 0;
}
//Traversal to get parent directory
int vfs_walk_parent(vfs_inode_t *dir, const char *path, 
                    vfs_inode_t **parent, char *name) {
    if (!dir || !path || !parent || !name) return -1;
    
    //Parsing the path into components
    char path_copy[PATH_MAX];
    strncpy(path_copy, path, PATH_MAX - 1);
    path_copy[PATH_MAX - 1] = '\0';
    
    char *saveptr;
    char *components[256];
    int comp_count = 0;
    char *token = strtok_r(path_copy, "/", &saveptr);
    
    while (token && comp_count < 256) {
        components[comp_count++] = token;
        token = strtok_r(NULL, "/", &saveptr);
    }
    
    if (comp_count == 0) {
        return -1;
    }
    
    //The last component is the name
    strncpy(name, components[comp_count - 1], NAME_MAX - 1);
    name[NAME_MAX - 1] = '\0';
    
    //If only one component is parent = dir
    if (comp_count == 1) {
        *parent = dir;
        vfs_inode_ref(*parent);
        return 0;
    }
    
    //Building the path to the parent directory
    char parent_path[PATH_MAX] = "/";
    for (int i = 0; i < comp_count - 1; i++) {
        if (strlen(parent_path) > 1) {
            strcat(parent_path, "/");
        }
        strcat(parent_path, components[i]);
    }
    
    //Getting the parent inode
    return vfs_walk(dir, parent_path, parent);
}

int vfs_lookup(vfs_inode_t *dir, const char *name, vfs_inode_t **result) {
    if (!dir || !name || !result) return -1;
    if (dir->i_mode != FT_DIR) return -1;
    if (!dir->i_op || !dir->i_op->lookup) return -1;
    
    int ret = dir->i_op->lookup(dir, name, result);
    if (ret == 0 && *result) {
        vfs_inode_ref(*result);
    }
    return ret;
}

int vfs_open(vfs_inode_t *dir, const char *path, u32 flags, vfs_file_t **file) {
    if (!dir || !path || !file) return -1;
    
    vfs_inode_t *inode = NULL;
    vfs_inode_t *parent = NULL;
    char name[NAME_MAX];
    
    //Trying to find the file
    if (vfs_walk(dir, path, &inode) == 0) {
        //The file exists
        if (flags & O_CREAT) {
            //O_CREAT with an existing file - just open it
        }
    } else {
        //File not found
        if (!(flags & O_CREAT)) return -1;
        
        //Create a new file
        if (vfs_walk_parent(dir, path, &parent, name) != 0) return -1;
        if (!parent->i_op || !parent->i_op->create) {
            if (parent) vfs_inode_unref(parent);
            return -1;
        }
        
        if (parent->i_op->create(parent, name, FT_REG_FILE, &inode) != 0) {
            vfs_inode_unref(parent);
            return -1;
        }
        vfs_inode_unref(parent);
    }
    
    //Create a file descriptor
    vfs_file_t *f = (vfs_file_t*)malloc(sizeof(vfs_file_t));
    if (!f) {
        vfs_inode_unref(inode);
        return -1;
    }
    
    f->f_inode = inode;
    f->f_pos = (flags & O_APPEND) ? inode->i_size : 0;
    f->f_flags = flags;
    f->f_private = NULL;
    
    *file = f;
    return 0;
}

int vfs_close(vfs_file_t *file) {
    if (!file) return -1;
    
    if (file->f_inode && file->f_inode->i_dirty) {
        vfs_sync(file->f_inode);
    }

    if (file->f_inode) {
        vfs_inode_unref(file->f_inode);
    }
    
    free(file);
    return 0;
}

int vfs_read(vfs_file_t *file, void *buf, u32 size, u32 *read) {
    if (!file || !file->f_inode || !file->f_inode->i_fop || 
        !file->f_inode->i_fop->read) return -1;
    
    int ret = file->f_inode->i_fop->read(file->f_inode, file->f_pos, 
                                          buf, size, read);
    if (ret == 0) {
        file->f_pos += *read;
    }
    return ret;
}

int vfs_write(vfs_file_t *file, const void *buf, u32 size, u32 *written) {
    if (!file || !file->f_inode || !file->f_inode->i_fop || 
        !file->f_inode->i_fop->write) return -1;
    
    int ret = file->f_inode->i_fop->write(file->f_inode, file->f_pos, 
                                           buf, size, written);
    if (ret == 0) {
        file->f_pos += *written;
        if (file->f_pos > file->f_inode->i_size) {
            file->f_inode->i_size = file->f_pos;
        }
        file->f_inode->i_dirty = 1;
    }
    return ret;
}

int vfs_seek(vfs_file_t *file, u64 offset, int whence) {
    if (!file || !file->f_inode) return -1;
    
    switch (whence) {
        case 0: // SEEK_SET
            file->f_pos = offset;
            break;
        case 1: // SEEK_CUR
            file->f_pos += offset;
            break;
        case 2: // SEEK_END
            file->f_pos = file->f_inode->i_size + offset;
            break;
        default:
            return -1;
    }
    
    return 0;
}

int vfs_mkdir(vfs_inode_t *dir, const char *path, u32 mode, vfs_inode_t **result) {
    if (!path) return -1;
    
    vfs_inode_t *parent;
    char name[NAME_MAX];
    
    if (vfs_resolve_path(dir, path, &parent, name, false) != 0) {
        return -1;
    }
    
    if (name[0] == '\0') {
        vfs_inode_unref(parent);
        return -1;  //can't create root
    }
    
    if (parent->i_mode != FT_DIR) {
        vfs_inode_unref(parent);
        return -1;
    }
    
    if (!parent->i_op || !parent->i_op->mkdir) {
        vfs_inode_unref(parent);
        return -1;
    }
    
    int ret = parent->i_op->mkdir(parent, name, FT_DIR, result);
    vfs_inode_unref(parent);
    return ret;
}

int vfs_rmdir(vfs_inode_t *dir, const char *path) {
    if (!path) return -1;
    
    vfs_inode_t *parent;
    char name[NAME_MAX];
    
    if (vfs_resolve_path(dir, path, &parent, name, false) != 0) {
        return -1;
    }
    
    if (name[0] == '\0') {
        vfs_inode_unref(parent);
        return -1;
    }
    
    if (!parent->i_op || !parent->i_op->rmdir) {
        vfs_inode_unref(parent);
        return -1;
    }
    
    int ret = parent->i_op->rmdir(parent, name);
    vfs_inode_unref(parent);
    return ret;
}

int vfs_readdir(vfs_inode_t *dir, u64 *pos, char *name, 
                u32 *name_len, u32 *type) {
    if (!dir || !pos || !name || !name_len || !type) {
        return -1;
    }
    
    //Checking that this is a directory
    if (dir->i_mode != FT_DIR) {
        return -1;
    }
    
    //Special case: VFS root directory
    if (dir == root_inode) {
        //First we show the mount points
        static mount_entry_t mount_entries[32];
        static int mount_count = -1;
        static int mount_index = 0;
        
        if (*pos == 0) {
            //Update the list of mount points
            mount_count = vfs_get_mount_points("/", mount_entries, 32);
            mount_index = 0;
        }
        
        //Showing mount points
        while (mount_index < mount_count) {
            if (mount_index == *pos) {
                strcpy(name, mount_entries[mount_index].name);
                *name_len = strlen(name);
                *type = mount_entries[mount_index].type;
                (*pos)++;
                mount_index++;
                return 0;
            }
            mount_index++;
        }
        
        //After the mount points we show the contents of the real file system
        if (dir->i_op && dir->i_op->readdir) {
            //Adjusting the position for the FS
            u64 fs_pos = *pos - mount_count;
            int ret = dir->i_op->readdir(dir, &fs_pos, name, name_len, type);
            if (ret == 0) {
                *pos = fs_pos + mount_count;
            }
            return ret;
        }
        
        return -1;
    }
    
    //For other directories - normal behavior
    if (!dir->i_op || !dir->i_op->readdir) {
        return -1;
    }
    
    return dir->i_op->readdir(dir, pos, name, name_len, type);
}

int vfs_create(vfs_inode_t *dir, const char *path, u32 mode, vfs_inode_t **result) {
    if (!path) return -1;
    
    vfs_inode_t *parent;
    char name[NAME_MAX];
    
    if (vfs_resolve_path(dir, path, &parent, name, false) != 0) {
        return -1;
    }
    
    if (name[0] == '\0') {
        vfs_inode_unref(parent);
        return -1;
    }
    
    if (parent->i_mode != FT_DIR) {
        vfs_inode_unref(parent);
        return -1;
    }
    
    if (!parent->i_op || !parent->i_op->create) {
        vfs_inode_unref(parent);
        return -1;
    }
    
    int ret = parent->i_op->create(parent, name, mode, result);
    vfs_inode_unref(parent);
    return ret;
}

int vfs_unlink(vfs_inode_t *dir, const char *path) {
    if (!path) return -1;
    
    vfs_inode_t *parent;
    char name[NAME_MAX];
    
    if (vfs_resolve_path(dir, path, &parent, name, false) != 0) {
        return -1;
    }
    
    if (name[0] == '\0') {
        vfs_inode_unref(parent);
        return -1;
    }
    
    if (!parent->i_op || !parent->i_op->unlink) {
        vfs_inode_unref(parent);
        return -1;
    }
    
    int ret = parent->i_op->unlink(parent, name);
    vfs_inode_unref(parent);
    return ret;
}

int vfs_rename(vfs_inode_t *old_dir, const char *old_path,
               vfs_inode_t *new_dir, const char *new_path) {
    if (!old_path || !new_path) return -1;
    
    vfs_inode_t *old_parent, *new_parent;
    char old_name[NAME_MAX], new_name[NAME_MAX];
    
    if (vfs_resolve_path(old_dir, old_path, &old_parent, old_name, false) != 0) {
        return -1;
    }
    
    if (vfs_resolve_path(new_dir, new_path, &new_parent, new_name, false) != 0) {
        vfs_inode_unref(old_parent);
        return -1;
    }
    
    if (old_name[0] == '\0' || new_name[0] == '\0') {
        vfs_inode_unref(old_parent);
        vfs_inode_unref(new_parent);
        return -1;
    }
    
    if (!old_parent->i_op || !old_parent->i_op->rename) {
        vfs_inode_unref(old_parent);
        vfs_inode_unref(new_parent);
        return -1;
    }
    
    int ret = old_parent->i_op->rename(old_parent, old_name, new_parent, new_name);
    vfs_inode_unref(old_parent);
    vfs_inode_unref(new_parent);
    return ret;
}

int vfs_stat(vfs_inode_t *inode, vfs_stat_t *stat) {
    if (!inode || !stat) return -1;
    
    stat->st_mode = inode->i_mode;
    stat->st_uid = inode->i_uid;
    stat->st_gid = inode->i_gid;
    stat->st_size = inode->i_size;
    stat->st_ctime = inode->i_ctime;
    stat->st_mtime = inode->i_mtime;
    stat->st_atime = inode->i_atime;
    stat->st_ino = inode->i_ino;
    stat->st_nlink = inode->i_nlink;
    
    return 0;
}

int vfs_chmod(vfs_inode_t *inode, u32 mode) {
    if (!inode) return -1;
    if (!inode->i_op || !inode->i_op->chmod) return -1;
    
    return inode->i_op->chmod(inode, mode);
}

int vfs_sync(vfs_inode_t *inode) {
    if (!inode) return -1;
    if (!inode->i_fop || !inode->i_fop->sync) return -1;
    
    int ret = inode->i_fop->sync(inode);
    if (ret == 0) inode->i_dirty = 0;
    return ret;
}

vfs_inode_t *vfs_alloc_inode(void) {
    vfs_inode_t *inode = (vfs_inode_t*)malloc(sizeof(vfs_inode_t));
    if (inode) {
        memset(inode, 0, sizeof(vfs_inode_t));
        inode->i_refcount = 1;
    }
    return inode;
}

void vfs_inode_ref(vfs_inode_t *inode) {
    if (inode) inode->i_refcount++;
}

void vfs_inode_unref(vfs_inode_t *inode) {
    if (!inode) return;
    if (--inode->i_refcount == 0) {
        if (inode->i_private) free(inode->i_private);
        free(inode);
    }
}

void vfs_free_inode(vfs_inode_t *inode) {
    vfs_inode_unref(inode);
}

int vfs_parent(vfs_inode_t *inode, vfs_inode_t **parent) {
    if (!inode || !parent) return -1;
    
    //If this is the root
    if (inode == root_inode) {
        *parent = root_inode;
        return 0;
    }
    
    //Calling the FS-specific implementation
    if (inode->i_op && inode->i_op->parent) {
        return inode->i_op->parent(inode, parent);
    }
    
    return -1;
}

int vfs_get_mount_points(const char *path, mount_entry_t *entries, int max_entries) {
    if (!path || !entries || max_entries <= 0) return -1;
    
    int count = 0;
    mount_point_t *mp = mounts;
    sz path_len = strlen(path);
    
    //We go through all the mount points
    while (mp && count < max_entries) {
        //Checking if the mount point is in this path
        if (strncmp(mp->path, path, path_len) == 0) {
            //We skip the path itself
            if (strcmp(mp->path, path) == 0) {
                mp = mp->next;
                continue;
            }
            
            //We get the next component after path
            const char *rest = mp->path + path_len;
            if (*rest == '/') rest++;
            
            //Finding the first component of the name
            const char *end = rest;
            while (*end && *end != '/') end++;
            
            int name_len = end - rest;
            if (name_len > 0 && name_len < 256) {
                strncpy(entries[count].name, rest, name_len);
                entries[count].name[name_len] = '\0';
                entries[count].inode = mp->inode;
                entries[count].type = FT_DIR;
                count++;
            }
        }
        mp = mp->next;
    }
    
    return count;
}
