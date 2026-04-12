#include <fs/vfs.h>
#include <kernel/types.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <kernel/blockdev.h>
#include <fs/exfat.h>
#include <fs/fat32.h>

extern char current_path[PATH_MAX];
extern vfs_inode_t *fs_root;
extern vfs_inode_t *devfs_root;

extern int vfs_get_name(vfs_inode_t *inode, char *name, int max_len);
extern int vfs_parent(vfs_inode_t *inode, vfs_inode_t **parent);

//Current path update function
static void update_current_path(void) {
    if (!fs_root || !current_dir) {
        strcpy(current_path, "/");
        return;
    }
    
    if (current_dir == fs_root) {
        strcpy(current_path, "/");
        return;
    }
    
    char temp[PATH_MAX];
    if (vfs_build_path(current_dir, temp, PATH_MAX) == 0) {
        strcpy(current_path, temp);
    } else {
        strcpy(current_path, "/");
    }
}

void cmd_cat(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: cat <filename>\n");
        return;
    }
    
    //Remove spaces at the beginning
    while (*args == ' ') args++;
    
    if (!fs_root) {
        terminal_error_printf("No filesystem mounted\n");
        return;
    }
    
    vfs_file_t *file;
    if (vfs_open(current_dir, args, O_READ, &file) == 0) {
        char buffer[512];
        u32 read;
        while (vfs_read(file, buffer, 512, &read) == 0 && read > 0) {
            buffer[read] = '\0';
            terminal_printf("%s", buffer);
        }
        terminal_printf("\n");
        vfs_close(file);
    } else {
        terminal_error_printf("File not found: %s\n", args);
    }
}

void cmd_format(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: format <fs> <disk_number>\n");
        terminal_printf("Filesystems: exfat, fat32\n");
        terminal_printf("Example: format fat32 1\n");
        return;
    }
    
    //Parse the arguments: the first word is FS, the second is disk number
    char fs_name[32];
    char disk_num_str[32];
    
    char *space = strchr(args, ' ');
    if (!space) {
        terminal_printf("Usage: format <fs> <disk_number>\n");
        return;
    }
    
    int name_len = space - args;
    if (name_len >= 32) name_len = 31;
    memcpy(fs_name, args, name_len);
    fs_name[name_len] = '\0';
    
    strcpy(disk_num_str, space + 1);
    int disk_num = atoi(disk_num_str);
    
    blockdev_t *disks[16];
    int count = blockdev_get_list(disks, 16);
    
    if (disk_num < 1 || disk_num > count) {
        terminal_error_printf("Invalid disk number\n");
        return;
    }
    
    blockdev_t *disk = disks[disk_num - 1];
    
    if (strcmp(fs_name, "exfat") == 0) {
        terminal_printf("Formatting %s as exFAT...\n", disk->name);
        if (exfat_format(disk) == 0) {
            terminal_printf("Format successful!\n");
            
            vfs_inode_t *root;
            if (vfs_mount("exfat", disk, &root) == 0) {
                if (fs_root) vfs_unmount(fs_root);
                fs_root = root;
                current_dir = fs_root;
                strcpy(current_path, "/");
                terminal_printf("Mounted %s as exFAT\n", disk->name);
            }
        } else {
            terminal_error_printf("Format failed!\n");
        }
    } 
    else if (strcmp(fs_name, "fat32") == 0) {
        terminal_printf("Formatting %s as FAT32...\n", disk->name);
        if (fat32_format(disk) == 0) {
            terminal_printf("Format successful!\n");
            
            vfs_inode_t *root;
            if (vfs_mount("fat32", disk, &root) == 0) {
                if (fs_root) vfs_unmount(fs_root);
                fs_root = root;
                current_dir = fs_root;
                strcpy(current_path, "/");
                terminal_printf("Mounted %s as FAT32\n", disk->name);
            }
        } else {
            terminal_error_printf("Format failed!\n");
        }
    }
    else {
        terminal_error_printf("Unknown filesystem: %s\n", fs_name);
        terminal_printf("Supported: exfat, fat32\n");
    }
}

void cmd_mount(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: mount <fs> <disk_number>\n");
        terminal_printf("Filesystems: exfat, fat32, ext2\n");
        terminal_printf("Example: mount fat32 1\n");
        
        blockdev_t *disks[16];
        int count = blockdev_get_list(disks, 16);
        
        terminal_printf("\nAvailable disks:\n");
        for (int i = 0; i < count; i++) {
            char size_str[32];
            u64 size_mb = disks[i]->total_bytes / (1024 * 1024);
            snprintf(size_str, sizeof(size_str), "%luMB", (unsigned long)size_mb);
            
            terminal_printf("  %d: %s [%s] - %s\n", 
                       i + 1, disks[i]->name, size_str,
                       disks[i]->type == BLOCKDEV_TYPE_AHCI ? "SATA" : "IDE");
        }
        return;
    }
    
    //Parsing arguments
    char fs_name[32];
    char disk_num_str[32];
    
    char *space = strchr(args, ' ');
    if (!space) {
        terminal_printf("Usage: mount <fs> <disk_number>\n");
        return;
    }
    
    int name_len = space - args;
    if (name_len >= 32) name_len = 31;
    memcpy(fs_name, args, name_len);
    fs_name[name_len] = '\0';
    
    strcpy(disk_num_str, space + 1);
    int disk_num = atoi(disk_num_str);
    
    blockdev_t *disks[16];
    int count = blockdev_get_list(disks, 16);
    
    if (disk_num < 1 || disk_num > count) {
        terminal_error_printf("Invalid disk number\n");
        return;
    }
    
    blockdev_t *disk = disks[disk_num - 1];
    
    terminal_printf("Mounting %s as %s...\n", disk->name, fs_name);
    
    vfs_inode_t *root;
    if (vfs_mount(fs_name, disk, &root) == 0) {
        if (fs_root) {
            vfs_unmount(fs_root);
        }
        fs_root = root;
        current_dir = fs_root;
        strcpy(current_path, "/");
        terminal_printf("Mounted %s as %s\n", disk->name, fs_name);
    } else {
        terminal_error_printf("Failed to mount %s as %s (wrong FS?)\n", disk->name, fs_name);
    }
}

void cmd_touch(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: touch <filename>\n");
        return;
    }
    
    while (*args == ' ') args++;
    
    if (!fs_root) {
        terminal_error_printf("No filesystem mounted\n");
        return;
    }
    
    vfs_inode_t *inode;
    if (vfs_create(current_dir, args, FT_REG_FILE, &inode) == 0) {
        terminal_printf("File created: %s\n", args);
    } else {
        terminal_error_printf("Failed to create file: %s\n", args);
    }
}

void cmd_mkdir(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: mkdir <dirname>\n");
        return;
    }
    
    while (*args == ' ') args++;
    
    if (!fs_root) {
        terminal_error_printf("No filesystem mounted\n");
        return;
    }
    
    vfs_inode_t *new_dir;
    if (vfs_mkdir(current_dir, args, FT_DIR, &new_dir) == 0) {
        terminal_printf("Directory created: %s\n", args);
        vfs_free_inode(new_dir);
    } else {
        terminal_error_printf("Failed to create directory: %s\n", args);
    }
}

void cmd_rm(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: rm <path>\n");
        return;
    }
    
    while (*args == ' ') args++;
    
    if (!fs_root) {
        terminal_error_printf("No filesystem mounted\n");
        return;
    }
    
    if (vfs_unlink(current_dir, args) == 0) {
        terminal_printf("Removed: %s\n", args);
    } else {
        terminal_error_printf("Failed to remove: %s\n", args);
    }
}

void cmd_write(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: write <filename> <text>\n");
        return;
    }
    
    //Parsing the arguments: the first word is the file name, the rest is text
    char filename[256];
    char text[256];
    char *space = strchr(args, ' ');
    
    if (!space) {
        terminal_printf("Usage: write <filename> <text>\n");
        return;
    }
    
    int name_len = space - args;
    if (name_len >= 256) name_len = 255;
    memcpy(filename, args, name_len);
    filename[name_len] = '\0';
    
    strcpy(text, space + 1);
    
    if (!fs_root) {
        terminal_error_printf("No filesystem mounted\n");
        return;
    }
    
    vfs_file_t *file;
    if (vfs_open(current_dir, filename, O_WRITE | O_CREAT, &file) == 0) {
        u32 written;
        if (vfs_write(file, text, strlen(text), &written) == 0) {
            terminal_printf("Written %u bytes to %s\n", written, filename);
        } else {
            terminal_error_printf("Write failed\n");
        }
        vfs_close(file);
    } else {
        terminal_error_printf("Failed to open %s\n", filename);
    }
}

void cmd_ls(char *args) {
    vfs_inode_t *target_dir;
    char *path = args;
    
    if (!fs_root) {
        terminal_error_printf("No filesystem mounted\n");
        return;
    }
    
    //If the path is not specified, use the current directory
    if (!path || path[0] == '\0') {
        target_dir = current_dir;
    } else {
        //Removing leading spaces
        while (*path == ' ') path++;
        
        vfs_inode_t *new_dir = NULL;
        
        if (path[0] == '/') {
            if (vfs_walk(fs_root, path, &new_dir) != 0) {
                terminal_error_printf("ls: %s: No such file or directory\n", path);
                return;
            }
        } else {
            if (vfs_walk(current_dir, path, &new_dir) != 0) {
                terminal_error_printf("ls: %s: No such file or directory\n", path);
                return;
            }
        }
        
        if (new_dir->i_mode != FT_DIR) {
            terminal_error_printf("ls: %s: Not a directory\n", path);
            vfs_free_inode(new_dir);
            return;
        }
        target_dir = new_dir;
    }
    
    if (target_dir == fs_root) {
        terminal_printf("\nContents of /:\n");
    } else if (target_dir == current_dir && (!args || args[0] == '\0')) {
        terminal_printf("\nContents of %s:\n", current_path);
    } else {
        terminal_printf("\nContents of %s:\n", path);
    }
    
    terminal_printf("%-4s %-30s %s\n", "Type", "Name", "Size");
    terminal_printf("---- ------------------------------ ----------\n");
    
    u64 pos = 0;
    char name[256];
    u32 name_len;
    u32 type;
    int count = 0;
    
    while (vfs_readdir(target_dir, &pos, name, &name_len, &type) == 0) {
        char type_char = (type == FT_DIR) ? 'D' : 'F';
        
        if (type == FT_REG_FILE) {
            vfs_inode_t *file_inode = NULL;
            if (vfs_lookup(target_dir, name, &file_inode) == 0) {
                terminal_printf("[%c]   %-30s %lu bytes\n", 
                           type_char, name, (unsigned long)file_inode->i_size);
                vfs_free_inode(file_inode);
            } else {
                terminal_printf("[%c]   %-30s\n", type_char, name);
            }
        } else {
            terminal_printf("[%c]   %-30s\n", type_char, name);
        }
        count++;
    }
    
    if (count == 0) {
        terminal_printf("  (empty directory)\n");
    }
    terminal_printf("\nTotal: %d items\n", count);
    
    if (target_dir != current_dir && target_dir != fs_root) {
        vfs_free_inode(target_dir);
    }
}

void cmd_rmdir(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: rmdir <dirname>\n");
        return;
    }
    
    while (*args == ' ') args++;
    
    if (!fs_root) {
        terminal_error_printf("No filesystem mounted\n");
        return;
    }
    
    if (vfs_rmdir(fs_root, args) == 0) {
        terminal_printf("Directory removed: %s\n", args);
    } else {
        terminal_error_printf("Failed to remove directory: %s (not empty or not a directory)\n", args);
    }
}

void cmd_cd(char *args) {
    if (!fs_root) {
        terminal_error_printf("No filesystem mounted\n");
        return;
    }
    
    vfs_inode_t *new_dir = NULL;
    
    if (!args || args[0] == '\0') {
        new_dir = fs_root;
        vfs_inode_ref(new_dir);
    } else {
        while (*args == ' ') args++;
        if (vfs_walk(current_dir, args, &new_dir) != 0) {
            terminal_error_printf("cd: %s: No such directory\n", args);
            return;
        }
    }
    
    if (new_dir->i_mode != FT_DIR) {
        terminal_error_printf("cd: %s: Not a directory\n", args);
        vfs_inode_unref(new_dir);
        return;
    }
    
    if (current_dir && current_dir != fs_root && current_dir != devfs_root) {
        vfs_inode_unref(current_dir);
    }
    
    current_dir = new_dir;
    update_current_path();
    terminal_update_prompt_line();
}

void cmd_pwd(void) {
    if (!fs_root) {
        terminal_error_printf("No filesystem mounted\n");
        return;
    }
    
    terminal_printf("%s\n", current_path);
}
