#include <kernel/types.h>
#include <kernel/terminal.h>
#include <fs/vfs.h>
#include <libk/string.h>
#include <kernel/findroot.h>

extern vfs_inode_t *fs_root;
extern vfs_inode_t *current_dir;
extern char current_path[PATH_MAX];
extern void cmd_ls(char *args);

void cmd_rootlist(void) {
    findroot_print_candidates();
}

void cmd_setroot(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: setroot <fs_name> [disk_num]\n");
        terminal_printf("Examples:\n");
        terminal_printf("  setroot exfat:sata_0\n");
        terminal_printf("  setroot exfat 0\n");
        terminal_printf("  setroot fat32:sata_1\n");
        return;
    }
    
    //Removing spaces
    while (*args == ' ') args++;
    
    //Parsing arguments
    char fs_name[64];
    int disk_num = -1;
    
    //Checking the format "exfat:sata_0" or "exfat 0"
    char *colon = strchr(args, ':');
    if (colon) {
        //Format "exfat:sata_0"
        int name_len = colon - args;
        if (name_len >= 64) name_len = 63;
        memcpy(fs_name, args, name_len);
        fs_name[name_len] = '\0';
        
        //Extracting the disk number from "sata_0"
        char *disk_str = colon + 1;
        if (strncmp(disk_str, "sata_", 5) == 0) {
            disk_num = atoi(disk_str + 5);
        } else if (strncmp(disk_str, "ide_", 4) == 0) {
            disk_num = atoi(disk_str + 4);
        } else {
            disk_num = atoi(disk_str);
        }
    } else {
        //Format "exfat 0" - two arguments
        char *space = strchr(args, ' ');
        if (!space) {
            terminal_error_printf("Invalid format. Use: setroot exfat:sata_0 or setroot exfat 0\n");
            return;
        }
        
        int name_len = space - args;
        if (name_len >= 64) name_len = 63;
        memcpy(fs_name, args, name_len);
        fs_name[name_len] = '\0';
        
        disk_num = atoi(space + 1);
    }
    
    if (disk_num < 0) {
        terminal_error_printf("Invalid disk number\n");
        return;
    }
    
    terminal_printf("Setting root to %s on disk %d...\n", fs_name, disk_num);
    
    if (findroot_set_root(fs_name, disk_num) == 0) {
        terminal_success_printf("Root changed! Refreshing...\n");
        terminal_refresh();

        fs_root = findroot_get_current();
        current_dir = fs_root;
        strcpy(current_path, "/");
        
        terminal_printf("New root contents:\n");
        cmd_ls("");
    } else {
        terminal_error_printf("Failed to set root\n");
    }
}

void cmd_chooseroot(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: chooseroot <index>\n");
        terminal_printf("Use 'rootlist' to see available roots\n");
        return;
    }
    
    while (*args == ' ') args++;
    int index = atoi(args);
    
    terminal_printf("Choosing root index %d...\n", index);
    
    if (findroot_choose_root(index) == 0) {
        terminal_success_printf("Root changed! Refreshing...\n");
        terminal_refresh();
        
        fs_root = findroot_get_current();
        current_dir = fs_root;
        strcpy(current_path, "/");
        
        terminal_printf("New root contents:\n");
        cmd_ls("");
    } else {
        terminal_error_printf("Failed to choose root\n");
    }
}
