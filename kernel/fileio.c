#include <kernel/fileio.h>
#include <fs/vfs.h>
#include <libk/string.h>
#include <mm/heap.h>

extern vfs_inode_t *fs_root;

static char* build_full_path(const char *parent, const char *name) {
    if (!parent || !name) return NULL;
    
    //Removing leading slashes
    while (*parent == '/') parent++;
    
    char *full_path = (char*)malloc(PATH_MAX);
    if (!full_path) return NULL;
    
    if (parent[0] == '\0') {
        //Root directory
        snprintf(full_path, PATH_MAX, "/%s", name);
    } else {
        snprintf(full_path, PATH_MAX, "/%s/%s", parent, name);
    }
    
    return full_path;
}

int create_dir(const char *parent, const char *name) {
    if (!parent || !name) return -1;
    
    char *full_path = build_full_path(parent, name);
    if (!full_path) return -1;
    
    vfs_inode_t *dir;
    int ret = vfs_mkdir(fs_root, full_path, FT_DIR, &dir);
    
    if (ret == 0) {
        vfs_free_inode(dir);
    }
    
    free(full_path);
    return ret;
}

int create_file(const char *parent, const char *name) {
    if (!parent || !name) return -1;
    
    char *full_path = build_full_path(parent, name);
    if (!full_path) return -1;
    
    vfs_file_t *file;
    int ret = vfs_open(fs_root, full_path, O_WRITE | O_CREAT, &file);
    
    if (ret == 0) {
        vfs_close(file);
    }
    
    free(full_path);
    return ret;
}

int create_file_with_content(const char *parent, const char *name, const char *content) {
    if (!parent || !name) return -1;
    
    char *full_path = build_full_path(parent, name);
    if (!full_path) return -1;
    
    vfs_file_t *file;
    if (vfs_open(fs_root, full_path, O_WRITE | O_CREAT, &file) != 0) {
        free(full_path);
        return -1;
    }
    
    int ret = 0;
    if (content && content[0]) {
        u32 written;
        ret = vfs_write(file, content, strlen(content), &written);
    }
    
    vfs_close(file);
    free(full_path);
    return ret;
}

int write_to_file(const char *path, const char *content) {
    if (!path || !content) return -1;
    
    vfs_file_t *file;
    if (vfs_open(fs_root, path, O_WRITE | O_CREAT | O_TRUNC, &file) != 0) {
        return -1;
    }
    
    u32 written;
    int ret = vfs_write(file, content, strlen(content), &written);
    vfs_close(file);
    return ret;
}

char* read_file(const char *path) {
    if (!path) return NULL;
    
    vfs_file_t *file;
    if (vfs_open(fs_root, path, O_READ, &file) != 0) {
        return NULL;
    }
    
    //Getting the file size
    vfs_stat_t stat;
    if (vfs_stat(file->f_inode, &stat) != 0) {
        vfs_close(file);
        return NULL;
    }
    
    char *buffer = (char*)malloc(stat.st_size + 1);
    if (!buffer) {
        vfs_close(file);
        return NULL;
    }
    
    u32 read;
    vfs_read(file, buffer, stat.st_size, &read);
    buffer[read] = '\0';
    
    vfs_close(file);
    return buffer;
}

int file_exists(const char *path) {
    if (!path) return 0;
    
    vfs_inode_t *inode;
    int ret = vfs_walk(fs_root, path, &inode);
    if (ret == 0) {
        vfs_free_inode(inode);
        return 1;
    }
    return 0;
}

int dir_exists(const char *path) {
    if (!path) return 0;
    
    vfs_inode_t *inode;
    int ret = vfs_walk(fs_root, path, &inode);
    if (ret == 0 && inode->i_mode == FT_DIR) {
        vfs_free_inode(inode);
        return 1;
    }
    if (inode) vfs_free_inode(inode);
    return 0;
}

int delete_file(const char *path) {
    if (!path) return -1;

    //Finding the parent directory
    char parent_path[PATH_MAX];
    char name[256];
    
    char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        strcpy(parent_path, "/");
        strcpy(name, path);
    } else {
        int parent_len = last_slash - path;
        if (parent_len == 0) {
            strcpy(parent_path, "/");
        } else {
            strncpy(parent_path, path, parent_len);
            parent_path[parent_len] = '\0';
        }
        strcpy(name, last_slash + 1);
    }
    
    vfs_inode_t *parent;
    if (vfs_walk(fs_root, parent_path, &parent) != 0) {
        return -1;
    }
    
    int ret = vfs_unlink(parent, name);
    vfs_free_inode(parent);
    return ret;
}

int delete_dir(const char *path) {
    if (!path) return -1;
    
    char parent_path[PATH_MAX];
    char name[256];
    
    char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        strcpy(parent_path, "/");
        strcpy(name, path);
    } else {
        int parent_len = last_slash - path;
        if (parent_len == 0) {
            strcpy(parent_path, "/");
        } else {
            strncpy(parent_path, path, parent_len);
            parent_path[parent_len] = '\0';
        }
        strcpy(name, last_slash + 1);
    }
    
    vfs_inode_t *parent;
    if (vfs_walk(fs_root, parent_path, &parent) != 0) {
        return -1;
    }
    
    int ret = vfs_rmdir(parent, name);
    vfs_free_inode(parent);
    return ret;
}

char* make_path(const char *parent, const char *name) {
    return build_full_path(parent, name);
}
