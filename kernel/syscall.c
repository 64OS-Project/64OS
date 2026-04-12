#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/terminal.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <ktime/clock.h>

/*
 * ============================================================================== Global Variables ======================================================================================
 */

extern vfs_inode_t *fs_root;
extern vfs_inode_t *current_dir;
extern char current_path[PATH_MAX];

/*
 * Current user (always root for now)
 */
static u32 current_uid = 0;
static u32 current_gid = 0;
static u32 current_euid = 0;
static u32 current_egid = 0;

/*
 * Simple file descriptors (simplified)
 */
#define MAX_FDS 256
static vfs_file_t *fd_table[MAX_FDS];
static int next_fd = 3;  /*
 * 0=stdin, 1=stdout, 2=stderr
 */

/*
 * =============================================================================== Auxiliary functions ================================================================================
 */

/*
 * Convert VFS stat to sys_stat
 */
static void vfs_stat_to_sys_stat(vfs_stat_t *vfs_stat, struct sys_stat *sys_stat) {
    memset(sys_stat, 0, sizeof(struct sys_stat));
    
    sys_stat->st_ino = vfs_stat->st_ino;
    sys_stat->st_mode = vfs_stat->st_mode;
    sys_stat->st_uid = vfs_stat->st_uid;
    sys_stat->st_gid = vfs_stat->st_gid;
    sys_stat->st_size = vfs_stat->st_size;
    sys_stat->st_mtime = vfs_stat->st_mtime;
    sys_stat->st_ctime = vfs_stat->st_ctime;
    sys_stat->st_atime = vfs_stat->st_atime;
    sys_stat->st_nlink = vfs_stat->st_nlink;
}

/*
 * Getting an inode along the way
 */
static vfs_inode_t *get_inode_by_path(const char *path) {
    if (!path) return NULL;
    
    vfs_inode_t *inode = NULL;
    
    if (path[0] == '/') {
        if (vfs_walk(fs_root, path, &inode) != 0) return NULL;
    } else {
        if (vfs_walk(current_dir, path, &inode) != 0) return NULL;
    }
    
    return inode;
}

/*
 * Allocating a new file descriptor
 */
static int alloc_fd(vfs_file_t *file) {
    for (int i = 3; i < MAX_FDS; i++) {
        if (fd_table[i] == NULL) {
            fd_table[i] = file;
            return i;
        }
    }
    return -1;
}

/*
 * ============================================================================== System calls ===================================================================================== System calls
 */

/*
 * SYS_EXIT - process termination
 */
static u64 sys_exit(u64 exit_code, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    terminal_printf("Process %d exited with code %d\n", 
                   get_current_task() ? get_current_task()->pid : 0, 
                   (int)exit_code);
    task_exit((int)exit_code);
    return 0;
}

/*
 * SYS_READ - reading from file
 */
static u64 sys_read(u64 fd, u64 buf, u64 count) {
    /*
 * stdin
 */
    if (fd == 0) {
        char *line = terminal_input(NULL);
        if (!line) return -1;
        
        u64 len = strlen(line);
        if (len > count) len = count;
        
        memcpy((void*)buf, line, len);
        return len;
    }
    
    /*
 * Regular file
 */
    if (fd < MAX_FDS && fd_table[fd]) {
        u32 read;
        int ret = vfs_read(fd_table[fd], (void*)buf, (u32)count, &read);
        if (ret != 0) return -1;
        return read;
    }
    
    return -1;
}

/*
 * SYS_WRITE - writing to a file
 */
static u64 sys_write(u64 fd, u64 buf, u64 count) {
    char *str = (char*)buf;
    
    /*
 * stdout/stderr
 */
    if (fd == 1 || fd == 2) {
        for (u64 i = 0; i < count && str[i]; i++) {
            terminal_putchar(str[i]);
        }
        return count;
    }
    
    /*
 * Regular file
 */
    if (fd < MAX_FDS && fd_table[fd]) {
        u32 written;
        int ret = vfs_write(fd_table[fd], str, (u32)count, &written);
        if (ret != 0) return -1;
        return written;
    }
    
    return -1;
}

/*
 * SYS_OPEN - opening a file
 */
static u64 sys_open(u64 path_ptr, u64 flags, u64 mode) {
    char *path = (char*)path_ptr;
    if (!path || !fs_root) return -1;
    
    u32 vfs_flags = 0;
    if (flags & O_RDONLY) vfs_flags |= O_READ;
    if (flags & O_WRONLY) vfs_flags |= O_WRITE;
    if (flags & O_RDWR) vfs_flags |= (O_READ | O_WRITE);
    if (flags & O_CREAT) vfs_flags |= O_CREAT;
    if (flags & O_TRUNC) vfs_flags |= O_TRUNC;
    if (flags & O_APPEND) vfs_flags |= O_APPEND;
    
    vfs_file_t *file;
    if (vfs_open(current_dir, path, vfs_flags, &file) != 0) {
        return -1;
    }
    
    int fd = alloc_fd(file);
    if (fd < 0) {
        vfs_close(file);
        return -1;
    }
    
    return fd;
}

/*
 * SYS_CLOSE - closing a file
 */
static u64 sys_close(u64 fd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (fd < 3 || fd >= MAX_FDS) return -1;
    if (!fd_table[fd]) return -1;
    
    int ret = vfs_close(fd_table[fd]);
    fd_table[fd] = NULL;
    return ret;
}

/*
 * SYS_GETPID - getting PID
 */
static u64 sys_getpid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *current = get_current_task();
    return current ? current->pid : 0;
}

/*
 * SYS_BRK - heap change (sbrk)
 */
static u64 sys_brk(u64 addr) {
    task_t *current = get_current_task();
    if (!current) return -1;
    
    if (addr == 0) {
        return (u64)current->program_break;
    }
    
    /*
 * Bounds checking
 */
    if (addr < (u64)current->heap_start) return -1;
    if (addr > (u64)current->heap_end) return -1;
    
    current->program_break = (void*)addr;
    return addr;
}

/*
 * SYS_STAT - file information
 */
static u64 sys_stat(u64 path_ptr, u64 stat_ptr) {
    char *path = (char*)path_ptr;
    struct sys_stat *stat_buf = (struct sys_stat*)stat_ptr;
    
    if (!path || !stat_buf) return -1;
    
    vfs_inode_t *inode = get_inode_by_path(path);
    if (!inode) return -1;
    
    vfs_stat_t vfs_stat_buf;
    if (vfs_stat(inode, &vfs_stat_buf) != 0) {
        vfs_free_inode(inode);
        return -1;
    }
    
    vfs_stat_to_sys_stat(&vfs_stat_buf, stat_buf);
    vfs_free_inode(inode);
    
    return 0;
}

/*
 * SYS_FSTAT - information about the open file
 */
static u64 sys_fstat(u64 fd, u64 stat_ptr) {
    struct sys_stat *stat_buf = (struct sys_stat*)stat_ptr;
    
    if (fd >= MAX_FDS || !fd_table[fd]) return -1;
    if (!stat_buf) return -1;
    
    vfs_stat_t vfs_stat_buf;
    if (vfs_stat(fd_table[fd]->f_inode, &vfs_stat_buf) != 0) return -1;
    
    vfs_stat_to_sys_stat(&vfs_stat_buf, stat_buf);
    return 0;
}

/*
 * SYS_LSEEK - move the pointer
 */
static u64 sys_lseek(u64 fd, u64 offset, u64 whence) {
    if (fd >= MAX_FDS || !fd_table[fd]) return -1;
    
    return vfs_seek(fd_table[fd], offset, whence);
}

/*
 * SYS_CHDIR - change directory
 */
static u64 sys_chdir(u64 path_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    char *path = (char*)path_ptr;
    if (!path) return -1;
    
    vfs_inode_t *new_dir = NULL;
    
    if (path[0] == '/') {
        if (vfs_walk(fs_root, path, &new_dir) != 0) return -1;
    } else {
        if (vfs_walk(current_dir, path, &new_dir) != 0) return -1;
    }
    
    if (new_dir->i_mode != FT_DIR) {
        vfs_free_inode(new_dir);
        return -1;
    }
    
    if (current_dir && current_dir != fs_root) {
        vfs_inode_unref(current_dir);
    }
    
    current_dir = new_dir;
    
    task_t *task = get_current_task();
    if (task) {
        vfs_build_path(current_dir, task->cwd_path, PATH_MAX);
    }
    
    return 0;
}

/*
 * SYS_GETCWD - getting the current path
 */
static u64 sys_getcwd(u64 buf_ptr, u64 size) {
    char *buf = (char*)buf_ptr;
    if (!buf || size == 0) return -1;
    
    char path[PATH_MAX];
    if (vfs_build_path(current_dir, path, PATH_MAX) != 0) return -1;
    
    u64 len = strlen(path) + 1;
    if (len > size) return -1;
    
    memcpy(buf, path, len);
    return 0;
}

/*
 * SYS_MKDIR - directory creation
 */
static u64 sys_mkdir(u64 path_ptr, u64 mode) {
    char *path = (char*)path_ptr;
    if (!path) return -1;
    
    vfs_inode_t *new_dir;
    if (vfs_mkdir(current_dir, path, FT_DIR, &new_dir) != 0) return -1;
    
    if (new_dir) vfs_free_inode(new_dir);
    return 0;
    
    (void)mode; /*
 * for now we ignore rights
 */
}

/*
 * SYS_RMDIR - deleting a directory
 */
static u64 sys_rmdir(u64 path_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    char *path = (char*)path_ptr;
    if (!path) return -1;
    
    return vfs_rmdir(current_dir, path);
}

/*
 * SYS_UNLINK - file deletion
 */
static u64 sys_unlink(u64 path_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    char *path = (char*)path_ptr;
    if (!path) return -1;
    
    return vfs_unlink(current_dir, path);
}

/*
 * SYS_RENAME - renaming
 */
static u64 sys_rename(u64 old_path_ptr, u64 new_path_ptr) {
    char *old_path = (char*)old_path_ptr;
    char *new_path = (char*)new_path_ptr;
    
    if (!old_path || !new_path) return -1;
    
    return vfs_rename(current_dir, old_path, current_dir, new_path);
}

/*
 * SYS_CHMOD - change rights
 */
static u64 sys_chmod(u64 path_ptr, u64 mode) {
    char *path = (char*)path_ptr;
    if (!path) return -1;
    
    vfs_inode_t *inode = get_inode_by_path(path);
    if (!inode) return -1;
    
    int ret = vfs_chmod(inode, mode);
    vfs_free_inode(inode);
    return ret;
}

/*
 * SYS_GETUID - get UID
 */
static u64 sys_getuid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return current_uid;
}

/*
 * SYS_GETEUID - get the effective UID
 */
static u64 sys_geteuid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return current_euid;
}

/*
 * SYS_SETUID - set UID (always allowed for now)
 */
static u64 sys_setuid(u64 uid) {
    current_uid = uid;
    current_euid = uid;
    return 0;
}

/*
 * SYS_TIME - get time
 */
static u64 sys_time(u64 tloc_ptr) {
    u64 epoch = system_clock.epoch;
    
    if (tloc_ptr) {
        *(u64*)tloc_ptr = epoch;
    }
    
    return epoch;
}

/*
 * SYS_SYNC - FS synchronization
 */
static u64 sys_sync(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (fs_root && fs_root->i_fop && fs_root->i_fop->sync) {
        return fs_root->i_fop->sync(fs_root);
    }
    return -1;
}

/*
 * SYS_GETDENTS - read directory
 */
static u64 sys_getdents(u64 fd, u64 dirp_ptr, u64 count) {
    if (fd >= MAX_FDS || !fd_table[fd]) return -1;
    
    vfs_file_t *file = fd_table[fd];
    if (!file->f_inode || file->f_inode->i_mode != FT_DIR) return -1;
    
    char *dirp = (char*)dirp_ptr;
    u64 pos = file->f_pos;
    u64 written = 0;
    
    /*
 * Recording format: inode(8) + offset(8) + reclen(2) + namelen(2) + name
 */
    while (written + 280 < count) {
        char name[256];
        u32 name_len;
        u32 type;
        
        if (vfs_readdir(file->f_inode, &pos, name, &name_len, &type) != 0) {
            break;
        }
        
        /*
 * Post title
 */
        u64 d_ino = 1;  /*
 * TODO: real inode
 */
        u64 d_off = pos;
        u16 d_reclen = sizeof(d_ino) + sizeof(d_off) + sizeof(u16) * 2 + name_len + 1;
        u16 d_namlen = name_len;
        
        memcpy(dirp + written, &d_ino, sizeof(d_ino));
        written += sizeof(d_ino);
        
        memcpy(dirp + written, &d_off, sizeof(d_off));
        written += sizeof(d_off);
        
        memcpy(dirp + written, &d_reclen, sizeof(d_reclen));
        written += sizeof(d_reclen);
        
        memcpy(dirp + written, &d_namlen, sizeof(d_namlen));
        written += sizeof(d_namlen);
        
        memcpy(dirp + written, name, name_len);
        written += name_len;
        
        dirp[written++] = '\0';
    }
    
    file->f_pos = pos;
    return written;
}

/*
 * SYS_IOCTL - device control (minimal)
 */
static u64 sys_ioctl(u64 fd, u64 request, u64 arg) {
    /*
 * TODO: ioctl support for terminal
 */
    (void)fd;
    (void)request;
    (void)arg;
    return -1;
}

/*
 * SYS_DUP - file descriptor duplication
 */
static u64 sys_dup(u64 old_fd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (old_fd >= MAX_FDS || !fd_table[old_fd]) return -1;
    
    int new_fd = alloc_fd(fd_table[old_fd]);
    if (new_fd < 0) return -1;
    
    fd_table[old_fd]->f_inode->i_refcount++;
    return new_fd;
}

/*
 * SYS_PIPE - creating a channel (simplified)
 */
static u64 sys_pipe(u64 pipefd_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)pipefd_ptr; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    terminal_warn_printf("sys_pipe: not implemented\n");
    return -1;
}

/*
 * =============================================================================== System Call Table =================================================================================
 */

typedef u64 (*syscall_fn_t)(u64, u64, u64, u64, u64, u64);

static const syscall_fn_t syscall_table[] = {
    [SYS_EXIT]      = (syscall_fn_t)sys_exit,
    [SYS_READ]      = (syscall_fn_t)sys_read,
    [SYS_WRITE]     = (syscall_fn_t)sys_write,
    [SYS_OPEN]      = (syscall_fn_t)sys_open,
    [SYS_CLOSE]     = (syscall_fn_t)sys_close,
    [SYS_GETPID]    = (syscall_fn_t)sys_getpid,
    [SYS_BRK]       = (syscall_fn_t)sys_brk,
    [SYS_STAT]      = (syscall_fn_t)sys_stat,
    [SYS_LSEEK]     = (syscall_fn_t)sys_lseek,
    [SYS_CHDIR]     = (syscall_fn_t)sys_chdir,
    [SYS_GETCWD]    = (syscall_fn_t)sys_getcwd,
    [SYS_MKDIR]     = (syscall_fn_t)sys_mkdir,
    [SYS_RMDIR]     = (syscall_fn_t)sys_rmdir,
    [SYS_UNLINK]    = (syscall_fn_t)sys_unlink,
    [SYS_RENAME]    = (syscall_fn_t)sys_rename,
    [SYS_CHMOD]     = (syscall_fn_t)sys_chmod,
    [SYS_GETUID]    = (syscall_fn_t)sys_getuid,
    [SYS_GETEUID]   = (syscall_fn_t)sys_geteuid,
    [SYS_SETUID]    = (syscall_fn_t)sys_setuid,
    [SYS_TIME]      = (syscall_fn_t)sys_time,
    [SYS_SYNC]      = (syscall_fn_t)sys_sync,
    [SYS_GETDENTS]  = (syscall_fn_t)sys_getdents,
    [SYS_IOCTL]     = (syscall_fn_t)sys_ioctl,
    [SYS_DUP]       = (syscall_fn_t)sys_dup,
    [SYS_PIPE]      = (syscall_fn_t)sys_pipe,
    [SYS_FSTAT]     = (syscall_fn_t)sys_fstat,
};

#define SYSCALL_COUNT (sizeof(syscall_table) / sizeof(syscall_table[0]))

/*
 * =============================================================================== Handler (called from asm) ================================================================================
 */

u64 syscall_handler(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    if (num >= SYSCALL_COUNT || !syscall_table[num]) {
        terminal_error_printf("syscall %d: invalid\n", (int)num);
        return -1;
    }
    
    return syscall_table[num](a1, a2, a3, a4, a5, a6);
}

/*
 * =============================================================================== Initialization ======================================================================================
 */

void syscall_init(void) {
    /*
 * Initializing the file descriptor table
 */
    memset(fd_table, 0, sizeof(fd_table));
    next_fd = 3;
    
    /*
 * Standard streams are not yet associated with real files
 */
    
    terminal_success_printf("[SYSCALL] %d syscalls available\n", SYSCALL_COUNT);
}
