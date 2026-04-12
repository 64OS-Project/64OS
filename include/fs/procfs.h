// include/fs/procfs.h
#ifndef PROCFS_H
#define PROCFS_H

#include <fs/vfs.h>

void procfs_init(void);
extern vfs_inode_t *procfs_root;

#endif
