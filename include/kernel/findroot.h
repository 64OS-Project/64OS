#ifndef FINDROOT_H
#define FINDROOT_H

#include <fs/vfs.h>
#include <kernel/blockdev.h>

//Structure for storing information about the found root file system
typedef struct {
    vfs_inode_t *root_inode;
    blockdev_t *device;
    char fs_name[32];
    char mount_point[64];
    bool is_active;
} root_candidate_t;

//Main functions
void findroot_init(void);
vfs_inode_t* findroot_scan_and_mount(void);
int findroot_set_root(const char *fs_name, int disk_num);
int findroot_choose_root(int index);
void findroot_remove_all_markers(void);
void findroot_print_candidates(void);

//Auxiliary functions
bool findroot_has_marker(vfs_inode_t *fs);
int findroot_create_marker(vfs_inode_t *fs);
int findroot_delete_marker(vfs_inode_t *fs);
vfs_inode_t* findroot_get_current(void);
void findroot_switch_to(vfs_inode_t *new_root);

#endif
