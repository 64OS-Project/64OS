#ifndef SYSCALL_H
#define SYSCALL_H

#include <kernel/types.h>

/*
 * =============================================================================== System call numbers (implemented only) ================================================================================
 */

#define SYS_EXIT           0
#define SYS_READ           1
#define SYS_WRITE          2
#define SYS_OPEN           3
#define SYS_CLOSE          4
#define SYS_GETPID         5
#define SYS_BRK            6
#define SYS_STAT           7
#define SYS_LSEEK          8
#define SYS_CHDIR          9
#define SYS_GETCWD        10
#define SYS_MKDIR         11
#define SYS_RMDIR         12
#define SYS_UNLINK        13
#define SYS_RENAME        14
#define SYS_CHMOD         15
#define SYS_GETUID        16
#define SYS_GETEUID       17
#define SYS_SETUID        18
#define SYS_TIME          19
#define SYS_SYNC          20
#define SYS_GETDENTS      21
#define SYS_IOCTL         22
#define SYS_DUP           23
#define SYS_PIPE          24
#define SYS_FSTAT         25

/*
 * =============================================================================== Constants (only what is supported) ================================================================================
 */

/*
 * Flags for open
 */
#ifndef O_RDONLY
#define O_RDONLY        O_READ      /*
 * 1
 */
#endif

#ifndef O_WRONLY
#define O_WRONLY        O_WRITE     /*
 * 2
 */
#endif

#ifndef O_RDWR
#define O_RDWR          (O_READ | O_WRITE)  /*
 * 3
 */
#endif

/*
 * Access modes
 */
#define S_IRUSR         0400
#define S_IWUSR         0200
#define S_IXUSR         0100
#define S_IRGRP         0040
#define S_IWGRP         0020
#define S_IXGRP         0010
#define S_IROTH         0004
#define S_IWOTH         0002
#define S_IXOTH         0001

/*
 * File types for stat
 */
#define S_IFMT          0170000
#define S_IFDIR         0040000
#define S_IFCHR         0020000
#define S_IFBLK         0060000
#define S_IFREG         0100000
#define S_IFIFO         0010000

/*
 * Seek whence
 */
#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2

/*
 * ============================================================================== Structures ========================================================================================
 */

/*
 * stat structure (POSIX)
 */
struct sys_stat {
    u64 st_ino;         /*
 * Inode number
 */
    u32 st_mode;        /*
 * Permissions and file type
 */
    u32 st_uid;         /*
 * Owner ID
 */
    u32 st_gid;         /*
 * Group ID
 */
    u64 st_size;        /*
 * Size in bytes
 */
    u64 st_mtime;       /*
 * Time of last modification
 */
    u64 st_ctime;       /*
 * Time of last status change
 */
    u64 st_atime;       /*
 * Last access time
 */
    u64 st_nlink;       /*
 * Number of hard links
 */
};

/*
 * ============================================================================================= Kernel API =====================================================================================
 */

void syscall_init(void);
u64 syscall_handler(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6);

#endif
