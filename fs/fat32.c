#include <fs/fat32.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <ktime/clock.h>
#include <ktime/rtc.h>

//==================== INTERNAL FUNCTIONS ====================

static int fat32_read_sectors(fat32 *fat, u32 sector, u32 count, void *buf) {
    return blockdev_read(fat->dev, sector, count, buf);
}

static int fat32_write_sectors(fat32 *fat, u32 sector, u32 count, void *buf) {
    return blockdev_write(fat->dev, sector, count, buf);
}

static u32 fat32_next_cluster(fat32 *fat, u32 cluster) {
    if (cluster < 2 || cluster >= fat->fat_entries) return FAT32_EOC;
    u32 next = fat->fat_cache[cluster];
    if (next >= FAT32_EOC) return FAT32_EOC;
    if (next == FAT32_FREE || next == FAT32_BAD) return FAT32_EOC;
    return next;
}

static int fat32_read_cluster(fat32 *fat, u32 cluster, u8 *buffer) {
    if (cluster < 2) return -1;
    u32 sector = fat->data_start + (cluster - 2) * fat->sectors_per_cluster;
    return fat32_read_sectors(fat, sector, fat->sectors_per_cluster, buffer);
}

static int fat32_write_cluster(fat32 *fat, u32 cluster, u8 *buffer) {
    if (cluster < 2) return -1;
    u32 sector = fat->data_start + (cluster - 2) * fat->sectors_per_cluster;
    return fat32_write_sectors(fat, sector, fat->sectors_per_cluster, buffer);
}

static u32 fat32_alloc_cluster(fat32 *fat) {
    // Use FSInfo hint
    u32 start = fat->fsinfo.next_free;
    if (start < 2 || start >= fat->fat_entries) start = 2;
    
    for (u32 i = 0; i < fat->fat_entries - 2; i++) {
        u32 cluster = start + i;
        if (cluster >= fat->fat_entries) cluster = 2 + (cluster - fat->fat_entries);
        
        if (fat->fat_cache[cluster] == FAT32_FREE) {
            fat->fat_cache[cluster] = FAT32_EOC;
            fat->fsinfo.free_count--;
            fat->fsinfo.next_free = cluster + 1;
            if (fat->fsinfo.next_free >= fat->fat_entries) fat->fsinfo.next_free = 2;
            fat->dirty = 1;
            return cluster;
        }
    }
    return 0;
}

static void fat32_free_cluster_chain(fat32 *fat, u32 cluster) {
    while (cluster >= 2 && cluster < fat->fat_entries) {
        u32 next = fat->fat_cache[cluster];
        fat->fat_cache[cluster] = FAT32_FREE;
        fat->fsinfo.free_count++;
        cluster = (next < FAT32_EOC) ? next : 0;
        fat->dirty = 1;
    }
}

static u32 fat32_alloc_clusters(fat32 *fat, u32 count, u32 *first) {
    *first = 0;
    u32 prev = 0;
    
    for (u32 i = 0; i < count; i++) {
        u32 cluster = fat32_alloc_cluster(fat);
        if (!cluster) {
            // Free allocated so far
            if (*first) fat32_free_cluster_chain(fat, *first);
            return 0;
        }
        
        if (!*first) *first = cluster;
        if (prev) fat->fat_cache[prev] = cluster;
        prev = cluster;
    }
    
    return count;
}

static void fat32_update_fsinfo(fat32 *fat) {
    if (!fat->dirty) return;
    
    u8 sector[512];
    memset(sector, 0, 512);
    
    fat32_fsinfo_t *fsinfo = (fat32_fsinfo_t*)sector;
    fsinfo->lead_signature = 0x41615252;
    fsinfo->struct_signature = 0x61417272;
    fsinfo->free_count = fat->fsinfo.free_count;
    fsinfo->next_free = fat->fsinfo.next_free;
    fsinfo->trail_signature = 0xAA550000;
    
    fat32_write_sectors(fat, fat->fsinfo_sector, 1, sector);
    fat->dirty = 0;
}

static u8 fat32_checksum(const u8 *name) {
    u8 sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i];
    }
    return sum;
}

static void fat32_name_from_entry(char *dest, u8 *name) {
    int i, j = 0;
    
    // Copy name
    for (i = 0; i < 8 && name[i] != ' '; i++)
        dest[j++] = name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i];
    
    // Copy extension if exists
    if (name[8] != ' ') {
        dest[j++] = '.';
        for (i = 8; i < 11 && name[i] != ' '; i++)
            dest[j++] = name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i];
    }
    
    dest[j] = '\0';
}

static void fat32_name_to_entry(u8 *dest, const char *name, u8 *checksum) {
    // Clear with spaces
    for (int i = 0; i < 11; i++)
        dest[i] = ' ';
    
    int i = 0;
    int j = 0;
    
    // Find dot
    while (name[i] && name[i] != '.' && i < 8) {
        dest[j++] = (name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i];
        i++;
    }
    
    // Skip dot
    if (name[i] == '.') i++;
    
    // Extension
    j = 8;
    while (name[i] && j < 11) {
        dest[j++] = (name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i];
        i++;
    }
    
    if (checksum) *checksum = fat32_checksum(dest);
}

static int fat32_create_lfn_entries(fat32 *fat, u8 *cluster_buf, int *offset,
                                     const char *name, u8 checksum, int is_last) {
    sz name_len = strlen(name);
    int num_entries = (name_len + 12) / 13; // 13 chars per LFN entry
    
    for (int i = num_entries - 1; i >= 0; i--) {
        fat32_lfn_entry_t *lfn = (fat32_lfn_entry_t*)(cluster_buf + *offset);
        memset(lfn, 0, sizeof(fat32_lfn_entry_t));
        
        lfn->order = (i + 1) | (is_last && i == num_entries - 1 ? 0x40 : 0);
        lfn->attr = FAT_ATTR_LFN;
        lfn->type = 0;
        lfn->checksum = checksum;
        lfn->first_cluster = 0;
        
        // Copy name parts
        int name_pos = i * 13;
        for (int j = 0; j < 5 && name_pos < (int)name_len; j++, name_pos++)
            lfn->name1[j] = name[name_pos];
        for (int j = 0; j < 6 && name_pos < (int)name_len; j++, name_pos++)
            lfn->name2[j] = name[name_pos];
        for (int j = 0; j < 2 && name_pos < (int)name_len; j++, name_pos++)
            lfn->name3[j] = name[name_pos];
        
        *offset += 32;
    }
    
    return num_entries;
}

//==================== VFS OPERATIONS ====================

static int fat32_lookup(vfs_inode_t *dir, const char *name, vfs_inode_t **result) {
    fat32_inode_private_t *priv = (fat32_inode_private_t*)dir->i_private;
    fat32 *fat = priv->fat;
    
    u32 cluster = priv->first_cluster;
    u8 buf[fat->bytes_per_cluster];
    u32 entry_index = 0;
    
    while (cluster && cluster < FAT32_EOC) {
        if (fat32_read_cluster(fat, cluster, buf) != 0) return -1;
        
        fat32_dir_entry_t *entry = (fat32_dir_entry_t*)buf;
        for (u32 i = 0; i < fat->bytes_per_cluster / 32; i++, entry_index++) {
            if (entry->name[0] == 0) return -1; // end of directory
            if (entry->name[0] == 0xE5) { // deleted
                entry++;
                continue;
            }
            
            // Skip LFN entries
            if (entry->attr == FAT_ATTR_LFN) {
                entry++;
                continue;
            }
            
            char entry_name[256];
            fat32_name_from_entry(entry_name, entry->name);
            
            if (strcmp(entry_name, name) == 0) {
                // Found!
                vfs_inode_t *inode = vfs_alloc_inode();
                fat32_inode_private_t *new_priv = malloc(sizeof(fat32_inode_private_t));
                
                u32 first_cluster = (entry->first_cluster_hi << 16) | entry->first_cluster_lo;
                
                new_priv->fat = fat;
                new_priv->first_cluster = first_cluster;
                new_priv->dir_cluster = cluster;
                new_priv->dir_entry = entry_index;
                new_priv->entry_offset = i * 32;
                new_priv->parent_cluster = priv->first_cluster;
                
                inode->i_mode = (entry->attr & FAT_ATTR_DIRECTORY) ? FT_DIR : FT_REG_FILE;
                inode->i_size = entry->file_size;
                inode->i_private = new_priv;
                inode->i_op = dir->i_op;
                inode->i_fop = dir->i_fop;
                
                *result = inode;
                return 0;
            }
            
            entry++;
        }
        
        cluster = fat32_next_cluster(fat, cluster);
    }
    
    return -1;
}

static int fat32_read(vfs_inode_t *inode, u64 offset, void *buf, 
                      u32 size, u32 *read) {
    fat32_inode_private_t *priv = (fat32_inode_private_t*)inode->i_private;
    fat32 *fat = priv->fat;
    
    if (offset >= inode->i_size) {
        *read = 0;
        return 0;
    }
    
    u32 to_read = size;
    if (offset + to_read > inode->i_size)
        to_read = inode->i_size - offset;
    
    u8 *buffer = (u8*)buf;
    u32 done = 0;
    u32 cluster = priv->first_cluster;
    u32 cluster_size = fat->bytes_per_cluster;
    
    // Skip to offset
    u32 skip_clusters = offset / cluster_size;
    for (u32 i = 0; i < skip_clusters && cluster && cluster < FAT32_EOC; i++)
        cluster = fat32_next_cluster(fat, cluster);
    
    if (!cluster || cluster >= FAT32_EOC) return -1;
    
    u32 cluster_offset = offset % cluster_size;
    u8 temp[cluster_size];
    
    while (done < to_read && cluster && cluster < FAT32_EOC) {
        if (fat32_read_cluster(fat, cluster, temp) != 0) return -1;
        
        u32 copy = cluster_size - cluster_offset;
        if (copy > to_read - done) copy = to_read - done;
        
        memcpy(buffer + done, temp + cluster_offset, copy);
        
        done += copy;
        cluster_offset = 0;
        cluster = fat32_next_cluster(fat, cluster);
    }
    
    *read = done;
    return 0;
}

static int fat32_write(vfs_inode_t *inode, u64 offset, const void *buf,
                       u32 size, u32 *written) {
    fat32_inode_private_t *priv = (fat32_inode_private_t*)inode->i_private;
    fat32 *fat = priv->fat;
    
    if (size == 0) {
        *written = 0;
        return 0;
    }
    
    u32 cluster_size = fat->bytes_per_cluster;
    u8 *buffer = (u8*)buf;
    u32 to_write = size;
    u32 done = 0;
    
    // Calculate clusters needed
    u32 first_cluster_idx = offset / cluster_size;
    u32 last_cluster_idx = (offset + size - 1) / cluster_size;
    u32 clusters_needed = last_cluster_idx - first_cluster_idx + 1;
    
    // Get or allocate cluster chain
    u32 cluster = priv->first_cluster;
    u32 cluster_idx = 0;
    
    // Skip to needed cluster
    for (u32 i = 0; i < first_cluster_idx; i++) {
        if (!cluster || cluster >= FAT32_EOC) {
            // Need to allocate
            if (!priv->first_cluster) {
                // Empty file - allocate first cluster
                if (!fat32_alloc_clusters(fat, 1, &priv->first_cluster))
                    return -1;
                cluster = priv->first_cluster;
            } else {
                return -1; // Hole not supported
            }
        } else {
            cluster = fat32_next_cluster(fat, cluster);
        }
    }
    
    // Ensure we have enough clusters
    u32 *clusters = malloc(clusters_needed * sizeof(u32));
    if (!clusters) return -1;
    
    u32 c = 0;
    while (c < clusters_needed) {
        if (!cluster || cluster >= FAT32_EOC) {
            // Need new cluster
            u32 new_cluster = fat32_alloc_cluster(fat);
            if (!new_cluster) {
                free(clusters);
                return -1;
            }
            
            if (c == 0 && !priv->first_cluster) {
                priv->first_cluster = new_cluster;
            } else if (c > 0) {
                fat->fat_cache[clusters[c-1]] = new_cluster;
            }
            
            clusters[c] = new_cluster;
            cluster = new_cluster;
        } else {
            clusters[c] = cluster;
            cluster = fat32_next_cluster(fat, cluster);
        }
        c++;
    }
    
    // Write data
    for (u32 i = 0; i < clusters_needed; i++) {
        u32 current = clusters[i];
        u32 cluster_offset = (i == 0) ? (offset % cluster_size) : 0;
        u32 write_size = cluster_size - cluster_offset;
        if (write_size > to_write - done) write_size = to_write - done;
        
        if (write_size == cluster_size) {
            fat32_write_cluster(fat, current, buffer + done);
        } else {
            u8 temp[cluster_size];
            fat32_read_cluster(fat, current, temp);
            memcpy(temp + cluster_offset, buffer + done, write_size);
            fat32_write_cluster(fat, current, temp);
        }
        
        done += write_size;
    }
    
    free(clusters);
    
    // Update file size
    if (offset + done > inode->i_size) {
        inode->i_size = offset + done;
        
        // Update directory entry
        u8 dir_buf[fat->bytes_per_cluster];
        fat32_read_cluster(fat, priv->dir_cluster, dir_buf);
        
        fat32_dir_entry_t *entry = (fat32_dir_entry_t*)(dir_buf + priv->entry_offset);
        entry->file_size = inode->i_size;
        
        // Update time
        u32 hh, mm, ss;
        read_rtc_time(&hh, &mm, &ss);
        u32 year, month, day;
        read_rtc_date(&year, &month, &day);
        
        entry->wrt_date = FAT_DATE(year, month, day);
        entry->wrt_time = FAT_TIME(hh, mm, ss);
        
        fat32_write_cluster(fat, priv->dir_cluster, dir_buf);
    }
    
    *written = done;
    return 0;
}

static int fat32_readdir(vfs_inode_t *dir, u64 *pos, char *name, 
                         u32 *name_len, u32 *type) {
    fat32_inode_private_t *priv = (fat32_inode_private_t*)dir->i_private;
    fat32 *fat = priv->fat;
    
    u32 entry_index = *pos;
    u32 entries_per_cluster = fat->bytes_per_cluster / 32;
    u32 cluster_index = entry_index / entries_per_cluster;
    u32 cluster_offset = entry_index % entries_per_cluster;
    
    u32 cluster = priv->first_cluster;
    for (u32 i = 0; i < cluster_index; i++) {
        if (!cluster || cluster >= FAT32_EOC) return -1;
        cluster = fat32_next_cluster(fat, cluster);
    }
    
    if (!cluster || cluster >= FAT32_EOC) return -1;
    
    u8 buf[fat->bytes_per_cluster];
    if (fat32_read_cluster(fat, cluster, buf) != 0) return -1;
    
    fat32_dir_entry_t *entry = (fat32_dir_entry_t*)buf + cluster_offset;
    
    // Skip deleted and LFN entries
    while (entry->name[0] == 0xE5 || entry->attr == FAT_ATTR_LFN) {
        entry_index++;
        cluster_offset++;
        
        if (cluster_offset >= entries_per_cluster) {
            cluster = fat32_next_cluster(fat, cluster);
            if (!cluster || cluster >= FAT32_EOC) return -1;
            if (fat32_read_cluster(fat, cluster, buf) != 0) return -1;
            cluster_offset = 0;
            entry = (fat32_dir_entry_t*)buf;
        } else {
            entry++;
        }
    }
    
    if (entry->name[0] == 0) return -1; // end
    
    fat32_name_from_entry(name, entry->name);
    *name_len = strlen(name);
    *type = (entry->attr & FAT_ATTR_DIRECTORY) ? FT_DIR : FT_REG_FILE;
    *pos = entry_index + 1;
    
    return 0;
}

static int fat32_create(vfs_inode_t *dir, const char *name, u32 mode, vfs_inode_t **result) {
    fat32_inode_private_t *priv = (fat32_inode_private_t*)dir->i_private;
    fat32 *fat = priv->fat;
    
    // Check if already exists
    vfs_inode_t *existing;
    if (fat32_lookup(dir, name, &existing) == 0) {
        vfs_free_inode(existing);
        return -1;
    }
    
    // Find free entry in directory
    u32 cluster = priv->first_cluster;
    u8 buf[fat->bytes_per_cluster];
    u32 entry_index = 0;
    u32 target_cluster = 0;
    u32 target_offset = 0;
    int found_free = 0;
    
    while (cluster && cluster < FAT32_EOC) {
        if (fat32_read_cluster(fat, cluster, buf) != 0) return -1;
        
        for (u32 i = 0; i < fat->bytes_per_cluster / 32; i++) {
            fat32_dir_entry_t *entry = (fat32_dir_entry_t*)buf + i;
            
            if (entry->name[0] == 0 || entry->name[0] == 0xE5) {
                target_cluster = cluster;
                target_offset = i * 32;
                found_free = 1;
                break;
            }
            entry_index++;
        }
        if (found_free) break;
        
        cluster = fat32_next_cluster(fat, cluster);
    }
    
    if (!found_free) {
        // Need to extend directory
        u32 new_cluster = fat32_alloc_cluster(fat);
        if (!new_cluster) return -1;
        
        if (!priv->first_cluster) {
            priv->first_cluster = new_cluster;
        } else {
            // Find last cluster
            u32 last = priv->first_cluster;
            u32 next;
            while ((next = fat32_next_cluster(fat, last)) && next < FAT32_EOC)
                last = next;
            fat->fat_cache[last] = new_cluster;
        }
        
        target_cluster = new_cluster;
        target_offset = 0;
        
        // Initialize new cluster
        memset(buf, 0, fat->bytes_per_cluster);
        fat32_write_cluster(fat, new_cluster, buf);
    }
    
    // Read target cluster
    if (target_cluster != cluster)
        fat32_read_cluster(fat, target_cluster, buf);
    
    // Create LFN entries if needed
    u8 short_name[11];
    u8 checksum;
    fat32_name_to_entry(short_name, name, &checksum);
    
    int name_len = strlen(name);
    int lfn_entries = (name_len > 12) ? (name_len + 12) / 13 : 0;
    int total_entries = 1 + lfn_entries;
    
    // Check if we have enough space in this cluster
    if (target_offset / 32 + total_entries > fat->bytes_per_cluster / 32) {
        // Need new cluster for directory entries
        // Simplified - just fail for now
        return -1;
    }
    
    int offset = target_offset;
    
    // Create LFN entries (in reverse order)
    if (lfn_entries > 0) {
        fat32_create_lfn_entries(fat, buf, &offset, name, checksum, 1);
    }
    
    // Create main entry
    fat32_dir_entry_t *entry = (fat32_dir_entry_t*)(buf + offset);
    memset(entry, 0, sizeof(fat32_dir_entry_t));
    memcpy(entry->name, short_name, 11);
    entry->attr = (mode == FT_DIR) ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE;
    
    // Set time
    u32 hh, mm, ss;
    read_rtc_time(&hh, &mm, &ss);
    u32 year, month, day;
    read_rtc_date(&year, &month, &day);
    
    entry->crt_date = FAT_DATE(year, month, day);
    entry->crt_time = FAT_TIME(hh, mm, ss);
    entry->wrt_date = entry->crt_date;
    entry->wrt_time = entry->crt_time;
    entry->lst_acc_date = entry->crt_date;
    
    // Allocate first cluster for file/dir
    if (mode == FT_DIR) {
        u32 first_cluster = fat32_alloc_cluster(fat);
        if (!first_cluster) return -1;
        
        entry->first_cluster_lo = first_cluster & 0xFFFF;
        entry->first_cluster_hi = (first_cluster >> 16) & 0xFFFF;
        
        // Initialize directory with . and ..
        u8 dir_buf[fat->bytes_per_cluster];
        memset(dir_buf, 0, fat->bytes_per_cluster);
        
        // . entry
        fat32_dir_entry_t *dot = (fat32_dir_entry_t*)dir_buf;
        memset(dot->name, ' ', 11);
        dot->name[0] = '.';
        dot->attr = FAT_ATTR_DIRECTORY;
        dot->first_cluster_lo = first_cluster & 0xFFFF;
        dot->first_cluster_hi = (first_cluster >> 16) & 0xFFFF;
        dot->crt_date = entry->crt_date;
        dot->crt_time = entry->crt_time;
        
        // .. entry
        fat32_dir_entry_t *dotdot = (fat32_dir_entry_t*)(dir_buf + 32);
        memset(dotdot->name, ' ', 11);
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
        dotdot->attr = FAT_ATTR_DIRECTORY;
        dotdot->first_cluster_lo = priv->first_cluster & 0xFFFF;
        dotdot->first_cluster_hi = (priv->first_cluster >> 16) & 0xFFFF;
        dotdot->crt_date = entry->crt_date;
        dotdot->crt_time = entry->crt_time;
        
        fat32_write_cluster(fat, first_cluster, dir_buf);
    }
    
    fat32_write_cluster(fat, target_cluster, buf);
    
    // Create inode
    vfs_inode_t *inode = vfs_alloc_inode();
    fat32_inode_private_t *new_priv = malloc(sizeof(fat32_inode_private_t));
    
    u32 first_cluster = (entry->first_cluster_hi << 16) | entry->first_cluster_lo;
    
    new_priv->fat = fat;
    new_priv->first_cluster = first_cluster;
    new_priv->dir_cluster = target_cluster;
    new_priv->dir_entry = entry_index;
    new_priv->entry_offset = offset;
    new_priv->parent_cluster = priv->first_cluster;
    
    inode->i_mode = mode;
    inode->i_size = 0;
    inode->i_private = new_priv;
    inode->i_op = dir->i_op;
    inode->i_fop = dir->i_fop;
    
    *result = inode;
    return 0;
}

static int fat32_mkdir(vfs_inode_t *dir, const char *name, u32 mode, vfs_inode_t **result) {
    return fat32_create(dir, name, FT_DIR, result);
}

static int fat32_unlink(vfs_inode_t *dir, const char *name) {
    fat32_inode_private_t *priv = (fat32_inode_private_t*)dir->i_private;
    fat32 *fat = priv->fat;
    
    vfs_inode_t *inode;
    if (fat32_lookup(dir, name, &inode) != 0) return -1;
    
    fat32_inode_private_t *file_priv = (fat32_inode_private_t*)inode->i_private;
    
    // Free clusters
    if (file_priv->first_cluster)
        fat32_free_cluster_chain(fat, file_priv->first_cluster);
    
    // Mark directory entry as deleted
    u8 buf[fat->bytes_per_cluster];
    fat32_read_cluster(fat, file_priv->dir_cluster, buf);
    
    fat32_dir_entry_t *entry = (fat32_dir_entry_t*)(buf + file_priv->entry_offset);
    
    // Find and delete LFN entries too
    int entry_num = file_priv->entry_offset / 32;
    int lfn_count = 0;
    
    // Count LFN entries before this one
    for (int i = entry_num - 1; i >= 0; i--) {
        fat32_dir_entry_t *e = (fat32_dir_entry_t*)buf + i;
        if (e->attr == FAT_ATTR_LFN && (e->name[0] & 0x40))
            break;
        if (e->attr == FAT_ATTR_LFN)
            lfn_count++;
        else
            break;
    }
    
    // Delete LFN entries
    for (int i = 0; i < lfn_count; i++) {
        fat32_dir_entry_t *e = (fat32_dir_entry_t*)buf + (entry_num - lfn_count + i);
        e->name[0] = 0xE5;
    }
    
    // Delete main entry
    entry->name[0] = 0xE5;
    
    fat32_write_cluster(fat, file_priv->dir_cluster, buf);
    
    vfs_free_inode(inode);
    return 0;
}

static int fat32_rmdir(vfs_inode_t *dir, const char *name) {
    return fat32_unlink(dir, name); // FAT32 doesn't distinguish
}

static int fat32_rename(vfs_inode_t *old_dir, const char *old_name,
                        vfs_inode_t *new_dir, const char *new_name) {
    fat32_inode_private_t *old_priv = (fat32_inode_private_t*)old_dir->i_private;
    fat32 *fat = old_priv->fat;
    
    // Find file
    vfs_inode_t *file;
    if (fat32_lookup(old_dir, old_name, &file) != 0) return -1;
    
    fat32_inode_private_t *file_priv = (fat32_inode_private_t*)file->i_private;
    
    // Read old directory cluster
    u8 old_buf[fat->bytes_per_cluster];
    fat32_read_cluster(fat, file_priv->dir_cluster, old_buf);
    
    // Save entry data
    int old_entry_num = file_priv->entry_offset / 32;
    int lfn_count = 0;
    
    // Count LFN entries
    for (int i = old_entry_num - 1; i >= 0; i--) {
        fat32_dir_entry_t *e = (fat32_dir_entry_t*)old_buf + i;
        if (e->attr == FAT_ATTR_LFN && (e->name[0] & 0x40))
            break;
        if (e->attr == FAT_ATTR_LFN)
            lfn_count++;
        else
            break;
    }
    
    int total_entries = 1 + lfn_count;
    u8 entry_data[total_entries * 32];
    memcpy(entry_data, old_buf + (old_entry_num - lfn_count) * 32, total_entries * 32);
    
    // Mark old entries as deleted
    for (int i = 0; i < total_entries; i++) {
        fat32_dir_entry_t *e = (fat32_dir_entry_t*)old_buf + (old_entry_num - lfn_count + i);
        e->name[0] = 0xE5;
    }
    fat32_write_cluster(fat, file_priv->dir_cluster, old_buf);
    
    // Find free space in new directory
    fat32_inode_private_t *new_priv = (fat32_inode_private_t*)new_dir->i_private;
    u32 new_cluster = new_priv->first_cluster;
    u8 new_buf[fat->bytes_per_cluster];
    u32 new_offset = 0;
    int found = 0;
    
    while (new_cluster && new_cluster < FAT32_EOC) {
        fat32_read_cluster(fat, new_cluster, new_buf);
        
        for (u32 i = 0; i < fat->bytes_per_cluster / 32; i++) {
            fat32_dir_entry_t *e = (fat32_dir_entry_t*)new_buf + i;
            if (e->name[0] == 0 || e->name[0] == 0xE5) {
                new_offset = i * 32;
                found = 1;
                break;
            }
        }
        if (found) break;
        
        new_cluster = fat32_next_cluster(fat, new_cluster);
    }
    
    if (!found) {
        // Need to extend directory
        u32 extra = fat32_alloc_cluster(fat);
        if (!extra) {
            // Restore old entries
            memcpy(old_buf + (old_entry_num - lfn_count) * 32, entry_data, total_entries * 32);
            fat32_write_cluster(fat, file_priv->dir_cluster, old_buf);
            return -1;
        }
        
        if (!new_priv->first_cluster) {
            new_priv->first_cluster = extra;
        } else {
            u32 last = new_priv->first_cluster;
            u32 next;
            while ((next = fat32_next_cluster(fat, last)) && next < FAT32_EOC)
                last = next;
            fat->fat_cache[last] = extra;
        }
        
        new_cluster = extra;
        memset(new_buf, 0, fat->bytes_per_cluster);
        new_offset = 0;
    }
    
    // Update name in saved entries
    u8 short_name[11];
    u8 checksum;
    fat32_name_to_entry(short_name, new_name, &checksum);
    
    // Update main entry
    fat32_dir_entry_t *main_entry = (fat32_dir_entry_t*)(entry_data + (lfn_count * 32));
    memcpy(main_entry->name, short_name, 11);
    
    // Update checksum in LFN entries
    for (int i = 0; i < lfn_count; i++) {
        fat32_lfn_entry_t *lfn = (fat32_lfn_entry_t*)(entry_data + i * 32);
        lfn->checksum = checksum;
    }
    
    // Write to new directory
    memcpy(new_buf + new_offset, entry_data, total_entries * 32);
    fat32_write_cluster(fat, new_cluster, new_buf);
    
    // Update inode private data
    file_priv->dir_cluster = new_cluster;
    file_priv->dir_entry = (new_offset / 32) + lfn_count;
    file_priv->entry_offset = new_offset + lfn_count * 32;
    file_priv->parent_cluster = new_priv->first_cluster;
    
    vfs_free_inode(file);
    return 0;
}

int fat32_get_name(vfs_inode_t *inode, char *name, int max_len) {
    fat32_inode_private_t *priv = (fat32_inode_private_t*)inode->i_private;
    fat32 *fat = priv->fat;
    
    if (priv->first_cluster == fat->root_cluster) {
        strcpy(name, "");
        return 0;
    }
    
    u8 buf[fat->bytes_per_cluster];
    if (fat32_read_cluster(fat, priv->dir_cluster, buf) != 0)
        return -1;
    
    fat32_dir_entry_t *entry = (fat32_dir_entry_t*)(buf + priv->entry_offset);
    fat32_name_from_entry(name, entry->name);
    
    return 0;
}

static int fat32_chmod(vfs_inode_t *inode, u32 mode) {
    // FAT32 doesn't have real permissions
    inode->i_mode = mode;
    return 0;
}

static int fat32_stat(vfs_inode_t *inode, void *stat_buf) {
    return vfs_stat(inode, (vfs_stat_t*)stat_buf);
}

static int fat32_sync(vfs_inode_t *inode) {
    fat32_inode_private_t *priv = (fat32_inode_private_t*)inode->i_private;
    fat32 *fat = priv->fat;
    
    if (fat->dirty) {
        // Write FAT
        fat32_write_sectors(fat, fat->fat_start, fat->bpb.fat_size_32, fat->fat_cache);
        fat32_update_fsinfo(fat);
    }
    
    return 0;
}

static int fat32_parent(vfs_inode_t *inode, vfs_inode_t **parent) {
    fat32_inode_private_t *priv = (fat32_inode_private_t*)inode->i_private;
    fat32 *fat = priv->fat;
    
    if (priv->first_cluster == fat->root_cluster) {
        *parent = inode;
        return 0;
    }
    
    vfs_inode_t *parent_inode = vfs_alloc_inode();
    fat32_inode_private_t *parent_priv = malloc(sizeof(fat32_inode_private_t));
    
    parent_priv->fat = fat;
    parent_priv->first_cluster = priv->parent_cluster;
    parent_priv->dir_cluster = 0;
    parent_priv->dir_entry = 0;
    parent_priv->entry_offset = 0;
    parent_priv->parent_cluster = fat->root_cluster;
    
    parent_inode->i_mode = FT_DIR;
    parent_inode->i_private = parent_priv;
    parent_inode->i_op = inode->i_op;
    parent_inode->i_fop = inode->i_fop;
    
    *parent = parent_inode;
    return 0;
}

static vfs_operations_t fat32_i_op = {
    .lookup = fat32_lookup,
    .readdir = fat32_readdir,
    .create = fat32_create,
    .mkdir = fat32_mkdir,
    .unlink = fat32_unlink,
    .rmdir = fat32_rmdir,
    .rename = fat32_rename,
    .chmod = fat32_chmod,
    .stat = fat32_stat,
    .get_name = fat32_get_name,
    .parent = fat32_parent,
};

static vfs_file_operations_t fat32_f_op = {
    .read = fat32_read,
    .write = fat32_write,
    .sync = fat32_sync,
};

//==================== MOUNTING ====================

static int fat32_mount(blockdev_t *dev, vfs_inode_t **root) {
    terminal_printf("[FAT32] Mounting...\n");
    
    fat32 *fat = malloc(sizeof(fat32));
    if (!fat) return -1;
    memset(fat, 0, sizeof(fat32));
    
    fat->dev = dev;
    
    // Read BPB
    u8 sector[512];
    if (fat32_read_sectors(fat, 0, 1, sector) != 0) {
        free(fat);
        return -1;
    }
    
    fat32_bpb_t *bpb = (fat32_bpb_t*)sector;
    
    // Check FAT32 signature
    if (bpb->bytes_per_sector != 512 || bpb->sectors_per_cluster == 0 ||
        bpb->fat_size_32 == 0) {
        terminal_error_printf("[FAT32] Not a FAT32 volume\n");
        free(fat);
        return -1;
    }
    
    fat->bpb = *bpb;
    fat->bytes_per_sector = bpb->bytes_per_sector;
    fat->sectors_per_cluster = bpb->sectors_per_cluster;
    fat->bytes_per_cluster = fat->bytes_per_sector * fat->sectors_per_cluster;
    
    fat->fat_start = bpb->reserved_sectors;
    fat->data_start = fat->fat_start + bpb->num_fats * bpb->fat_size_32;
    fat->root_cluster = bpb->root_cluster;
    fat->fsinfo_sector = bpb->fs_info;
    
    fat->fat_entries = bpb->fat_size_32 * fat->bytes_per_sector / 4;
    fat->total_clusters = (bpb->total_sectors_32 - fat->data_start) / fat->sectors_per_cluster;
    
    terminal_printf("[FAT32] Cluster size: %d, Root cluster: %d, Total clusters: %d\n",
               fat->bytes_per_cluster, fat->root_cluster, fat->total_clusters);
    
    // Read FAT
    fat->fat_cache = malloc(bpb->fat_size_32 * 512);
    if (!fat->fat_cache) {
        free(fat);
        return -1;
    }
    
    if (fat32_read_sectors(fat, fat->fat_start, bpb->fat_size_32, fat->fat_cache) != 0) {
        free(fat->fat_cache);
        free(fat);
        return -1;
    }
    
    // Read FSInfo
    if (fat->fsinfo_sector) {
        fat32_read_sectors(fat, fat->fsinfo_sector, 1, &fat->fsinfo);
    } else {
        // Rebuild FSInfo
        fat->fsinfo.free_count = 0;
        fat->fsinfo.next_free = 2;
        for (u32 i = 2; i < fat->fat_entries; i++) {
            if (fat->fat_cache[i] == FAT32_FREE)
                fat->fsinfo.free_count++;
        }
    }
    
    // Create root inode
    vfs_inode_t *root_inode = vfs_alloc_inode();
    fat32_inode_private_t *priv = malloc(sizeof(fat32_inode_private_t));
    
    priv->fat = fat;
    priv->first_cluster = fat->root_cluster;
    priv->dir_cluster = 0;
    priv->dir_entry = 0;
    priv->entry_offset = 0;
    priv->parent_cluster = fat->root_cluster;
    
    root_inode->i_mode = FT_DIR;
    root_inode->i_size = 0;
    root_inode->i_private = priv;
    root_inode->i_op = &fat32_i_op;
    root_inode->i_fop = &fat32_f_op;
    
    *root = root_inode;
    
    terminal_printf("[FAT32] Mounted successfully\n");
    return 0;
}

static int fat32_unmount(vfs_inode_t *root) {
    fat32_inode_private_t *priv = (fat32_inode_private_t*)root->i_private;
    fat32 *fat = priv->fat;
    
    if (fat->dirty) {
        fat32_write_sectors(fat, fat->fat_start, fat->bpb.fat_size_32, fat->fat_cache);
        fat32_update_fsinfo(fat);
    }
    
    if (fat->fat_cache) free(fat->fat_cache);
    free(fat);
    free(priv);
    vfs_free_inode(root);
    
    return 0;
}

//==================== FORMATTING ====================

int fat32_format(blockdev_t *dev) {
    if (!dev || dev->status != BLOCKDEV_READY) {
        terminal_error_printf("[FAT32] Device not ready\n");
        return -1;
    }
    
    terminal_printf("[FAT32] Formatting %s...\n", dev->name);
    
    u32 sector_size = dev->sector_size;
    u64 total_sectors = dev->total_sectors;
    
    // Calculate parameters
    u8 sectors_per_cluster;
    if (total_sectors < 0x100000) sectors_per_cluster = 1;      // <512MB
    else if (total_sectors < 0x400000) sectors_per_cluster = 4; // <2GB
    else if (total_sectors < 0x1000000) sectors_per_cluster = 8; // <8GB
    else sectors_per_cluster = 16; // >8GB
    
    u32 reserved_sectors = 32;
    u32 fat_size = (total_sectors + sectors_per_cluster - 1) / sectors_per_cluster;
    fat_size = (fat_size * 4 + sector_size - 1) / sector_size;
    
    // Round up to cluster boundary
    u32 data_start = reserved_sectors + 2 * fat_size;
    u32 root_cluster = 2;
    
    terminal_printf("[FAT32] Sectors per cluster: %d, FAT size: %d sectors\n",
               sectors_per_cluster, fat_size);
    
    // Create BPB
    fat32_bpb_t bpb;
    memset(&bpb, 0, sizeof(bpb));
    
    // Jump boot
    bpb.jump_boot[0] = 0xEB;
    bpb.jump_boot[1] = 0x58;
    bpb.jump_boot[2] = 0x90;
    
    memcpy(bpb.oem_name, "ALKO    ", 8);
    bpb.bytes_per_sector = 512;
    bpb.sectors_per_cluster = sectors_per_cluster;
    bpb.reserved_sectors = reserved_sectors;
    bpb.num_fats = 2;
    bpb.root_entries = 0; // 0 for FAT32
    bpb.total_sectors_16 = 0;
    bpb.media_descriptor = 0xF8;
    bpb.fat_size_16 = 0; // 0 for FAT32
    bpb.sectors_per_track = 63;
    bpb.num_heads = 255;
    bpb.hidden_sectors = 0;
    bpb.total_sectors_32 = total_sectors;
    
    // FAT32 specific
    bpb.fat_size_32 = fat_size;
    bpb.extended_flags = 0;
    bpb.fs_version = 0;
    bpb.root_cluster = root_cluster;
    bpb.fs_info = 1;
    bpb.backup_boot_sector = 6;
    memset(bpb.reserved, 0, 12);
    bpb.drive_number = 0x80;
    bpb.reserved1 = 0;
    bpb.boot_signature = 0x29;
    bpb.volume_id = 0x12345678;
    memcpy(bpb.volume_label, "ALKDISK    ", 11);
    memcpy(bpb.fs_type, "FAT32   ", 8);
    
    // Boot signature
    u8 boot_sector[512];
    memcpy(boot_sector, &bpb, sizeof(bpb));
    boot_sector[510] = 0x55;
    boot_sector[511] = 0xAA;
    
    // Write boot sector
    if (blockdev_write(dev, 0, 1, boot_sector) != 0) {
        terminal_error_printf("[FAT32] Failed to write boot sector\n");
        return -1;
    }
    
    // Write backup boot sector
    blockdev_write(dev, 6, 1, boot_sector);
    
    // Create FSInfo
    fat32_fsinfo_t fsinfo;
    memset(&fsinfo, 0, sizeof(fsinfo));
    fsinfo.lead_signature = 0x41615252;
    fsinfo.struct_signature = 0x61417272;
    fsinfo.free_count = (total_sectors - data_start) / sectors_per_cluster;
    fsinfo.next_free = root_cluster + 1;
    fsinfo.trail_signature = 0xAA550000;
    
    // Write FSInfo
    blockdev_write(dev, 1, 1, &fsinfo);
    blockdev_write(dev, 7, 1, &fsinfo); // backup
    
    // Create FAT
    u32 *fat = malloc(fat_size * 512);
    if (!fat) return -1;
    memset(fat, 0, fat_size * 512);
    
    // Reserved clusters
    fat[0] = 0x0FFFFFF8;
    fat[1] = 0x0FFFFFFF;
    
    // Root directory cluster
    fat[root_cluster] = 0x0FFFFFFF;
    
    // Write FATs
    for (int i = 0; i < 2; i++) {
        blockdev_write(dev, reserved_sectors + i * fat_size, fat_size, fat);
    }
    free(fat);
    
    // Initialize root directory
    u8 *root_buf = malloc(sectors_per_cluster * 512);
    if (!root_buf) return -1;
    memset(root_buf, 0, sectors_per_cluster * 512);
    
    // Create . and .. entries
    fat32_dir_entry_t *dot = (fat32_dir_entry_t*)root_buf;
    memset(dot->name, ' ', 11);
    dot->name[0] = '.';
    dot->attr = FAT_ATTR_DIRECTORY;
    dot->first_cluster_lo = root_cluster & 0xFFFF;
    dot->first_cluster_hi = (root_cluster >> 16) & 0xFFFF;
    
    fat32_dir_entry_t *dotdot = (fat32_dir_entry_t*)(root_buf + 32);
    memset(dotdot->name, ' ', 11);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attr = FAT_ATTR_DIRECTORY;
    dotdot->first_cluster_lo = root_cluster & 0xFFFF;
    dotdot->first_cluster_hi = (root_cluster >> 16) & 0xFFFF;
    
    // Write root directory
    blockdev_write(dev, data_start, sectors_per_cluster, root_buf);
    free(root_buf);
    
    terminal_printf("[FAT32] Format complete\n");
    return 0;
}

static file_system_t fat32_fs = {
    .name = "fat32",
    .mount = fat32_mount,
    .unmount = fat32_unmount,
    .next = NULL
};

void fat32_init(void) {
    vfs_register_fs(&fat32_fs);
    terminal_printf("[FAT32] Driver initialized\n");
}
