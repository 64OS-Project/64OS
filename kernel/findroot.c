#include <kernel/findroot.h>
#include <kernel/blockdev.h>
#include <kernel/terminal.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/sched.h>

//Global Variables
static root_candidate_t g_candidates[16];
static int g_candidate_count = 0;
static vfs_inode_t *g_current_root = NULL;

//List of all mounted file systems (temporary)
typedef struct {
    vfs_inode_t *root;
    blockdev_t *dev;
    char name[32];
} mounted_fs_t;

static mounted_fs_t g_mounted_fs[32];
static int g_mounted_fs_count = 0;

//==================== INTERNAL FUNCTIONS ====================

//Checking if the FS has a .root marker
bool findroot_has_marker(vfs_inode_t *fs) {
    if (!fs || fs->i_mode != FT_DIR) return false;
    
    vfs_inode_t *marker;
    if (vfs_lookup(fs, ".root", &marker) == 0) {
        vfs_free_inode(marker);
        return true;
    }
    return false;
}

//Create a .root marker on the FS
int findroot_create_marker(vfs_inode_t *fs) {
    if (!fs) return -1;
    
    vfs_file_t *file;
    if (vfs_open(fs, ".root", O_WRITE | O_CREAT, &file) != 0) {
        return -1;
    }
    
    char marker_data[] = "root";
    u32 written;
    vfs_write(file, marker_data, sizeof(marker_data), &written);
    vfs_close(file);
    
    return 0;
}

//Remove .root marker from FS
int findroot_delete_marker(vfs_inode_t *fs) {
    if (!fs) return -1;
    return vfs_unlink(fs, ".root");
}

//Find all FS (already mounted via VFS)
static void scan_mounted_filesystems(void) {
    g_mounted_fs_count = 0;
    
    //Getting all block devices
    blockdev_t *disks[32];
    int disk_count = blockdev_get_list(disks, 32);
    
    for (int i = 0; i < disk_count; i++) {
        if (!disks[i] || disks[i]->status != BLOCKDEV_READY) continue;
        
        //Trying to mount exFAT
        vfs_inode_t *root;
        if (vfs_mount("exfat", disks[i], &root) == 0) {
            mounted_fs_t *fs = &g_mounted_fs[g_mounted_fs_count++];
            fs->root = root;
            fs->dev = disks[i];
            snprintf(fs->name, sizeof(fs->name), "exfat:%s", disks[i]->name);
            terminal_printf("[FINDROOT] Mounted exfat on %s\n", disks[i]->name);
            continue;
        }
        
        //Trying to mount FAT32
        if (vfs_mount("fat32", disks[i], &root) == 0) {
            mounted_fs_t *fs = &g_mounted_fs[g_mounted_fs_count++];
            fs->root = root;
            fs->dev = disks[i];
            snprintf(fs->name, sizeof(fs->name), "fat32:%s", disks[i]->name);
            terminal_printf("[FINDROOT] Mounted fat32 on %s\n", disks[i]->name);
            continue;
        }
    }
}

//Collect candidates with .root marker
static void collect_root_candidates(void) {
    g_candidate_count = 0;
    
    for (int i = 0; i < g_mounted_fs_count; i++) {
        mounted_fs_t *fs = &g_mounted_fs[i];
        
        if (findroot_has_marker(fs->root)) {
            root_candidate_t *cand = &g_candidates[g_candidate_count++];
            cand->root_inode = fs->root;
            cand->device = fs->dev;
            strcpy(cand->fs_name, fs->name);
            cand->is_active = false;
            
            terminal_printf("[FINDROOT] Found .root on %s\n", fs->name);
        }
    }
}

//==================== PUBLIC FUNCTIONS ====================

//Initializing the root search system
void findroot_init(void) {
    g_candidate_count = 0;
    g_mounted_fs_count = 0;
    g_current_root = NULL;
    terminal_printf("[FINDROOT] Initialized\n");
}

//Main function: scan everything, look for .root, return root
vfs_inode_t* findroot_scan_and_mount(void) {
    terminal_printf("[FINDROOT] Scanning all disks...\n");
    
    //1. We mount all possible FS
    scan_mounted_filesystems();
    
    if (g_mounted_fs_count == 0) {
        terminal_error_printf("[FINDROOT] No filesystems found!\n");
        return NULL;
    }
    
    terminal_printf("[FINDROOT] Found %d filesystems\n", g_mounted_fs_count);
    
    //2. Looking for the .root marker
    collect_root_candidates();
    
    if (g_candidate_count == 1) {
        //Found exactly one root
        g_current_root = g_candidates[0].root_inode;
        g_candidates[0].is_active = true;
        terminal_success_printf("[FINDROOT] Root found on %s\n", 
                               g_candidates[0].fs_name);
        return g_current_root;
        
    } else if (g_candidate_count > 1) {
        //Found some roots
        terminal_warn_printf("[FINDROOT] Multiple roots found!\n");
        findroot_print_candidates();
        return NULL;
        
    } else {
        //No root
        terminal_warn_printf("[FINDROOT] No .root marker found\n");
        return NULL;
    }
}

//Set the root to the specified file system (create .root)
int findroot_set_root(const char *fs_name, int disk_num) {
    //Looking for the right FS
    mounted_fs_t *target = NULL;
    for (int i = 0; i < g_mounted_fs_count; i++) {
        if (strcmp(g_mounted_fs[i].name, fs_name) == 0) {
            target = &g_mounted_fs[i];
            break;
        }
        //You can also search by disk number
        char disk_name[32];
        snprintf(disk_name, sizeof(disk_name), "sata_%d", disk_num);
        if (strstr(g_mounted_fs[i].name, disk_name)) {
            target = &g_mounted_fs[i];
            break;
        }
    }
    
    if (!target) {
        terminal_error_printf("[FINDROOT] Filesystem not found: %s %d\n", 
                             fs_name, disk_num);
        return -1;
    }
    
    //We remove markers from all other file systems
    findroot_remove_all_markers();
    
    //Create a marker on the target file system
    if (findroot_create_marker(target->root) != 0) {
        terminal_error_printf("[FINDROOT] Failed to create .root\n");
        return -1;
    }
    
    //Switching the root
    findroot_switch_to(target->root);
    
    terminal_success_printf("[FINDROOT] Root set to %s\n", target->name);
    return 0;
}

//Select a root from several candidates
int findroot_choose_root(int index) {
    if (index < 0 || index >= g_candidate_count) {
        terminal_error_printf("[FINDROOT] Invalid index %d\n", index);
        return -1;
    }
    
    //Removing markers from all others
    for (int i = 0; i < g_candidate_count; i++) {
        if (i != index) {
            findroot_delete_marker(g_candidates[i].root_inode);
        }
    }
    
    //Switching the root
    findroot_switch_to(g_candidates[index].root_inode);
    g_candidates[index].is_active = true;
    
    terminal_success_printf("[FINDROOT] Switched to %s\n", 
                           g_candidates[index].fs_name);
    return 0;
}

//Remove markers from all FS
void findroot_remove_all_markers(void) {
    for (int i = 0; i < g_mounted_fs_count; i++) {
        findroot_delete_marker(g_mounted_fs[i].root);
    }
    terminal_printf("[FINDROOT] All .root markers removed\n");
}

//Display a list of candidates
void findroot_print_candidates(void) {
    terminal_printf("\n=== Root candidates ===\n");
    for (int i = 0; i < g_candidate_count; i++) {
        terminal_printf("  %d: %s", i, g_candidates[i].fs_name);
        if (g_candidates[i].is_active) {
            terminal_printf(" [ACTIVE]");
        }
        terminal_printf("\n");
    }
    terminal_printf("=======================\n");
}

//Get current root
vfs_inode_t* findroot_get_current(void) {
    return g_current_root;
}

//Switch to new root
void findroot_switch_to(vfs_inode_t *new_root) {
    if (!new_root) return;
    
    //Save the old root if necessary
    vfs_inode_t *old_root = g_current_root;
    
    g_current_root = new_root;
    vfs_inode_ref(g_current_root);
    
    //Change the current directory for all processes
    task_t *current = get_current_task();
    if (current) {
        current->cwd_inode = g_current_root;
        strcpy(current->cwd_path, "/");
    }
    
    terminal_success_printf("[FINDROOT] Switched to new root\n");
}
