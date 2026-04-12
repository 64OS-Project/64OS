#include <fs/exfat.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>

//==================== INTERNAL FUNCTIONS ====================

//Reading sectors from disk
static int exfat_read_sectors(exfat_t *exfat, u64 sector, u32 count, void *buf) {
    return blockdev_read(exfat->dev, sector, count, buf);
}

//Read Cluster
static int exfat_read_cluster(exfat_t *exfat, u32 cluster, u8 *buffer) {
    if (cluster < 2) return -1;  //Clusters 0 and 1 are reserved
    
    u64 sector = exfat->cluster_heap_start / exfat->bytes_per_sector + 
                     (cluster - 2) * exfat->sectors_per_cluster;
    
    return exfat_read_sectors(exfat, sector, exfat->sectors_per_cluster, buffer);
}

//Cluster entry
static int exfat_write_cluster(exfat_t *exfat, u32 cluster, const u8 *buffer) {
    if (cluster < 2) return -1;
    
    u64 sector = exfat->cluster_heap_start / exfat->bytes_per_sector + 
                     (cluster - 2) * exfat->sectors_per_cluster;
    
    return blockdev_write(exfat->dev, sector, exfat->sectors_per_cluster, buffer);
}

//Get next cluster from FAT
static u32 exfat_next_cluster(exfat_t *exfat, u32 cluster) {
    if (cluster >= exfat->fat_entries) return 0;
    return exfat->fat_cache[cluster];
}

//Convert exFAT time to UNIX timestamp (simplified)
static u64 exfat_time_to_unix(u32 exfat_time) {
    // exFAT time: 2-second increments from 1980
    // UNIX time: seconds from 1970
    u64 days = (exfat_time >> 16) & 0xFFFF;  // Days from 1980
    u64 seconds = (exfat_time & 0xFFFF) * 2;
    
    // 1980 to 1970 offset: approximately 10 years of days
    return (days * 86400) + seconds + (10 * 365 * 86400);
}

//==================== VFS OPERATIONS ====================

//Directory search
static int exfat_lookup(vfs_inode_t *dir, const char *name, vfs_inode_t **result) {
    if (dir->i_mode != FT_DIR) return -1;
    
    exfat_inode_private_t *priv = (exfat_inode_private_t*)dir->i_private;
    exfat_t *exfat = priv->exfat;
    
    u32 cluster = priv->first_cluster;
    u8 buf[exfat->bytes_per_cluster];
    
    while (cluster >= 2 && cluster != EXFAT_FAT_END) {
        exfat_read_cluster(exfat, cluster, buf);
        
        u8 *ptr = buf;
        u8 *end = buf + exfat->bytes_per_cluster;
        
        while (ptr < end) {
            u8 type = ptr[0];
            if (type == 0) break;  //End of directory
            
            if (type == EXFAT_ENTRY_FILE) {
                exfat_file_entry_t *file = (exfat_file_entry_t*)ptr;
                
                //Looking for stream entry
                u8 count = file->secondary_count;
                u8 *stream_ptr = ptr + sizeof(exfat_file_entry_t);
                exfat_stream_entry_t *stream = NULL;
                
                for (int i = 0; i < count; i++) {
                    if (stream_ptr[0] == EXFAT_ENTRY_STREAM) {
                        stream = (exfat_stream_entry_t*)stream_ptr;
                        break;
                    }
                    stream_ptr += 32;
                }
                
                if (!stream) {
                    ptr += 32 * (count + 1);
                    continue;
                }
                
                //Looking for a name
                char ascii_name[256];
                int name_len = 0;
                u8 *name_ptr = stream_ptr + 32;  //After stream
                
                for (int i = 0; i < count - 1 && name_len < 255; i++) {
                    if (name_ptr[0] == EXFAT_ENTRY_NAME) {
                        exfat_name_entry_t *name_entry = (exfat_name_entry_t*)name_ptr;
                        for (int j = 0; j < 15; j++) {
                            u16 c = name_entry->name[j];
                            if (c == 0) break;
                            if (c < 128) ascii_name[name_len++] = (char)c;
                        }
                    }
                    name_ptr += 32;
                }
                ascii_name[name_len] = '\0';
                
                //Let's compare
                if (strcmp(ascii_name, name) == 0) {
                    //Found!
                    vfs_inode_t *inode = vfs_alloc_inode();
                    exfat_inode_private_t *new_priv = 
                        (exfat_inode_private_t*)malloc(sizeof(exfat_inode_private_t));
                    
                    new_priv->exfat = exfat;
                    new_priv->first_cluster = stream->first_cluster;
                    new_priv->data_length = stream->data_length;
                    new_priv->dir_cluster = cluster;
                    new_priv->dir_entry = (ptr - buf) / 32;
		    new_priv->parent_cluster = priv->first_cluster;
                    
                    inode->i_mode = (file->file_attributes & EXFAT_ATTR_DIRECTORY) ? 
                                    FT_DIR : FT_REG_FILE;
                    inode->i_size = stream->data_length;
                    inode->i_private = new_priv;
                    
                    //Copying operations
                    inode->i_op = dir->i_op;
                    inode->i_fop = dir->i_fop;
                    
                    *result = inode;
                    return 0;
                }
            }
            
            //Next entry
            u8 count = ptr[1] + 1;  //Main + secondary
            ptr += 32 * count;
        }
        
        cluster = exfat_next_cluster(exfat, cluster);
    }
    
    return -1;
}

static int exfat_create_file(vfs_inode_t *dir, const char *name, u32 mode, vfs_inode_t **result) {
    if (!dir || !name) return -1;
    if (dir->i_mode != FT_DIR) return -1;
    
    exfat_inode_private_t *dir_priv = (exfat_inode_private_t*)dir->i_private;
    exfat_t *exfat = dir_priv->exfat;
    
    //Checking to see if there is already such a name
    vfs_inode_t *existing = NULL;
    if (exfat_lookup(dir, name, &existing) == 0) {
        vfs_free_inode(existing);
        return -1;  //already exists
    }
    
    //Select a cluster for the file
    u32 new_cluster = 0;
    for (u32 i = 2; i < exfat->fat_entries; i++) {
        if (exfat->fat_cache[i] == EXFAT_FAT_FREE) {
            new_cluster = i;
            break;
        }
    }
    if (new_cluster == 0) return -1;  //no room
    
    exfat->fat_cache[new_cluster] = EXFAT_FAT_END;
    
    //Looking for free space in the parent directory
    u32 parent_cluster = dir_priv->first_cluster;
    u8 parent_buf[exfat->bytes_per_cluster];
    exfat_read_cluster(exfat, parent_cluster, parent_buf);
    
    //Looking for a free entry
    int free_entry = -1;
    u8 *ptr = parent_buf;
    for (int i = 0; i < exfat->bytes_per_cluster / 32; i++) {
        if (ptr[0] == 0) {
            free_entry = i;
            break;
        }
        ptr += 32;
    }
    if (free_entry == -1) return -1;
    
    //Create a file entry
    exfat_file_entry_t *file_entry = (exfat_file_entry_t*)ptr;
    memset(file_entry, 0, sizeof(exfat_file_entry_t));
    file_entry->type = EXFAT_ENTRY_FILE;
    file_entry->secondary_count = 2;  //stream + name
    file_entry->file_attributes = 0;  //regular file
    
    //Create a stream entry
    exfat_stream_entry_t *stream = (exfat_stream_entry_t*)(ptr + 32);
    memset(stream, 0, sizeof(exfat_stream_entry_t));
    stream->type = EXFAT_ENTRY_STREAM;
    stream->name_length = strlen(name);
    stream->first_cluster = new_cluster;
    stream->data_length = 0;
    
    //Create a name entry
    exfat_name_entry_t *name_entry = (exfat_name_entry_t*)(ptr + 64);
    memset(name_entry, 0, sizeof(exfat_name_entry_t));
    name_entry->type = EXFAT_ENTRY_NAME;
    
    //Copy the name
    for (int i = 0; i < strlen(name) && i < 15; i++) {
        name_entry->name[i] = name[i];
    }
    
    //Write it back
    exfat_write_cluster(exfat, parent_cluster, parent_buf);
    
    //Mark FAT as dirty
    exfat->fat_cache[0] |= 1;
    
    //Create an inode for a file
    if (result) {
        vfs_inode_t *inode = vfs_alloc_inode();
        exfat_inode_private_t *new_priv = malloc(sizeof(exfat_inode_private_t));
        
        new_priv->exfat = exfat;
        new_priv->first_cluster = new_cluster;
        new_priv->data_length = 0;
        new_priv->dir_cluster = parent_cluster;
        new_priv->dir_entry = free_entry;
        new_priv->parent_cluster = dir_priv->first_cluster;
        
        inode->i_mode = FT_REG_FILE;
        inode->i_size = 0;
        inode->i_private = new_priv;
        inode->i_op = dir->i_op;
        inode->i_fop = dir->i_fop;
        
        *result = inode;
    }
    
    return 0;
}

static int exfat_mkdir(vfs_inode_t *dir, const char *name, u32 mode, vfs_inode_t **new_inode);

//Creating a file
static int exfat_create(vfs_inode_t *dir, const char *name, u32 mode, vfs_inode_t **result) {
    //For directories we use mkdir
    if (mode == FT_DIR) {
        return exfat_mkdir(dir, name, mode, result);
    }
    
    //We use our own logic for files
    return exfat_create_file(dir, name, mode, result);
}

//Deleting a file
static int exfat_unlink(vfs_inode_t *dir, const char *name) {
    if (!dir || !name) return -1;
    if (dir->i_mode != FT_DIR) return -1;
    
    exfat_inode_private_t *dir_priv = (exfat_inode_private_t*)dir->i_private;
    exfat_t *exfat = dir_priv->exfat;
    
    //Looking for a file
    vfs_inode_t *file = NULL;
    if (exfat_lookup(dir, name, &file) != 0) return -1;
    
    exfat_inode_private_t *file_priv = (exfat_inode_private_t*)file->i_private;
    
    //Freeing file clusters in FAT
    u32 cluster = file_priv->first_cluster;
    while (cluster >= 2 && cluster != EXFAT_FAT_END) {
        u32 next = exfat_next_cluster(exfat, cluster);
        exfat->fat_cache[cluster] = EXFAT_FAT_FREE;
        cluster = next;
    }
    
    //Mark entries in the directory as free
    u8 buf[exfat->bytes_per_cluster];
    exfat_read_cluster(exfat, dir_priv->first_cluster, buf);
    
    //Finding the file entry
    u8 *ptr = buf + file_priv->dir_entry * 32;
    u8 count = ptr[1] + 1;  //main + secondary
    
    //Mark all entries as free (type = 0)
    for (int i = 0; i < count; i++) {
        ptr[i * 32] = 0;
    }
    
    //Write it back
    exfat_write_cluster(exfat, dir_priv->first_cluster, buf);
    
    //We note that FAT has changed
    exfat->fat_cache[0] |= 1;  // dirty flag
    
    vfs_free_inode(file);
    return 0;
}

//Creating a directory
//Creating a directory - now returns inode
static int exfat_mkdir(vfs_inode_t *dir, const char *name, u32 mode, vfs_inode_t **new_inode) {
    if (!dir || !name) return -1;
    if (dir->i_mode != FT_DIR) return -1;
    
    exfat_inode_private_t *dir_priv = (exfat_inode_private_t*)dir->i_private;
    exfat_t *exfat = dir_priv->exfat;
    
    //Checking to see if there is already such a name
    vfs_inode_t *existing = NULL;
    if (exfat_lookup(dir, name, &existing) == 0) {
        vfs_free_inode(existing);
        return -1;  //already exists
    }
    
    //Select a cluster for a new directory/file
    u32 new_cluster = 0;
    for (u32 i = 2; i < exfat->fat_entries; i++) {
        if (exfat->fat_cache[i] == EXFAT_FAT_FREE) {
            new_cluster = i;
            break;
        }
    }
    if (new_cluster == 0) return -1;  //no room
    
    exfat->fat_cache[new_cluster] = EXFAT_FAT_END;
    
    //Initialize a new directory (empty)
    u8 cluster_buf[exfat->bytes_per_cluster];
    memset(cluster_buf, 0, exfat->bytes_per_cluster);
    exfat_write_cluster(exfat, new_cluster, cluster_buf);
    
    //We are looking for free space in the parent directory for a new entry
    u32 parent_cluster = dir_priv->first_cluster;
    u8 parent_buf[exfat->bytes_per_cluster];
    exfat_read_cluster(exfat, parent_cluster, parent_buf);
    
    //Looking for a free entry
    int free_entry = -1;
    u8 *ptr = parent_buf;
    for (int i = 0; i < exfat->bytes_per_cluster / 32; i++) {
        if (ptr[0] == 0) {  //free entry
            free_entry = i;
            break;
        }
        ptr += 32;
    }
    if (free_entry == -1) return -1;  //no space in the directory
    
    //Create a file entry (main)
    exfat_file_entry_t *file_entry = (exfat_file_entry_t*)ptr;
    memset(file_entry, 0, sizeof(exfat_file_entry_t));
    file_entry->type = EXFAT_ENTRY_FILE;
    file_entry->secondary_count = 2;  //stream + name
    if (mode == FT_DIR) {
        file_entry->file_attributes = EXFAT_ATTR_DIRECTORY;
    } else {
        file_entry->file_attributes = 0;  //regular file
    }
    terminal_printf("[DEBUG] Creating '%s' with mode=%s, attributes=0x%x\n", 
           name, mode == FT_DIR ? "DIR" : "FILE", file_entry->file_attributes);
    file_entry->create_time = 0;
    file_entry->modify_time = 0;
    
    //Create a stream entry
    exfat_stream_entry_t *stream = (exfat_stream_entry_t*)(ptr + 32);
    memset(stream, 0, sizeof(exfat_stream_entry_t));
    stream->type = EXFAT_ENTRY_STREAM;
    stream->name_length = strlen(name);
    stream->first_cluster = new_cluster;
    stream->data_length = 0;
    
    //Create a name entry
    exfat_name_entry_t *name_entry = (exfat_name_entry_t*)(ptr + 64);
    memset(name_entry, 0, sizeof(exfat_name_entry_t));
    name_entry->type = EXFAT_ENTRY_NAME;
    
    //Copy the name (just ASCII)
    for (int i = 0; i < strlen(name) && i < 15; i++) {
        name_entry->name[i] = name[i];
    }
    
    //Write it back
    exfat_write_cluster(exfat, parent_cluster, parent_buf);
    
    //Mark FAT as dirty
    exfat->fat_cache[0] |= 1;
    
    //CREATE AN INODE FOR A NEW FILE/DIRECTORY
    if (new_inode) {
        vfs_inode_t *inode = vfs_alloc_inode();
        exfat_inode_private_t *new_priv = 
            (exfat_inode_private_t*)malloc(sizeof(exfat_inode_private_t));
        
        new_priv->exfat = exfat;
        new_priv->first_cluster = new_cluster;
        new_priv->data_length = 0;
        new_priv->dir_cluster = parent_cluster;
        new_priv->dir_entry = free_entry;
        new_priv->parent_cluster = dir_priv->first_cluster;  //IMPORTANT!
        
        inode->i_mode = mode;
        inode->i_size = 0;
        inode->i_private = new_priv;
        inode->i_op = dir->i_op;
        inode->i_fop = dir->i_fop;
        
        *new_inode = inode;
    }
    
    return 0;
}

//Removing a directory
static int exfat_rmdir(vfs_inode_t *dir, const char *name) {
    if (!dir || !name) return -1;
    if (dir->i_mode != FT_DIR) return -1;
    
    //Looking for a directory
    vfs_inode_t *subdir = NULL;
    if (exfat_lookup(dir, name, &subdir) != 0) return -1;
    
    if (subdir->i_mode != FT_DIR) {
        vfs_free_inode(subdir);
        return -1;  //not a directory
    }
    
    //Checking if the directory is empty
    //TODO: check that there are no files in it (except . and ..)
    
    //We use exfat_unlink (the same for files and empty directories)
    int ret = exfat_unlink(dir, name);
    vfs_free_inode(subdir);
    return ret;
}

//Renaming
static int exfat_rename(vfs_inode_t *old_dir, const char *old_name,
                        vfs_inode_t *new_dir, const char *new_name) {
    if (!old_dir || !old_name || !new_dir || !new_name) return -1;
    
    exfat_inode_private_t *old_priv = (exfat_inode_private_t*)old_dir->i_private;
    exfat_t *exfat = old_priv->exfat;
    
    //Finding the file
    vfs_inode_t *file = NULL;
    if (exfat_lookup(old_dir, old_name, &file) != 0) return -1;
    
    //Reading the cluster of the old directory
    u8 old_buf[exfat->bytes_per_cluster];
    exfat_read_cluster(exfat, old_priv->first_cluster, old_buf);
    
    //Finding the file entry
    exfat_inode_private_t *file_priv = (exfat_inode_private_t*)file->i_private;
    u8 *old_ptr = old_buf + file_priv->dir_entry * 32;
    u8 count = old_ptr[1] + 1;
    
    //Temporarily saving file data
    u8 file_data[count * 32];
    memcpy(file_data, old_ptr, count * 32);
    
    //Mark the old entry as free
    for (int i = 0; i < count; i++) {
        old_ptr[i * 32] = 0;
    }
    exfat_write_cluster(exfat, old_priv->first_cluster, old_buf);
    
    //Looking for a place in a new directory
    u8 new_buf[exfat->bytes_per_cluster];
    exfat_read_cluster(exfat, ((exfat_inode_private_t*)new_dir->i_private)->first_cluster, new_buf);
    
    //Looking for a free entry
    int free_entry = -1;
    u8 *new_ptr = new_buf;
    for (int i = 0; i < exfat->bytes_per_cluster / 32; i++) {
        if (new_ptr[0] == 0) {
            free_entry = i;
            break;
        }
        new_ptr += 32;
    }
    
    if (free_entry == -1) {
        //Restoring an old recording
        memcpy(old_ptr, file_data, count * 32);
        exfat_write_cluster(exfat, old_priv->first_cluster, old_buf);
        vfs_free_inode(file);
        return -1;
    }
    
    //Copy the file data to a new location
    memcpy(new_buf + free_entry * 32, file_data, count * 32);
    
    //Update the name in the name entry
    u8 *name_ptr = new_buf + (free_entry + 2) * 32;  //after file and stream
    exfat_name_entry_t *name_entry = (exfat_name_entry_t*)name_ptr;
    memset(name_entry->name, 0, 30);
    for (int i = 0; i < strlen(new_name) && i < 15; i++) {
        name_entry->name[i] = new_name[i];
    }
    
    exfat_write_cluster(exfat, ((exfat_inode_private_t*)new_dir->i_private)->first_cluster, new_buf);
    
    //Update dir_cluster and dir_entry in private file data
    file_priv->dir_cluster = ((exfat_inode_private_t*)new_dir->i_private)->first_cluster;
    file_priv->dir_entry = free_entry;
    
    vfs_free_inode(file);
    return 0;
}

//Reading a file
static int exfat_read(vfs_inode_t *inode, u64 offset, void *buf, 
                      u32 size, u32 *read) {
    exfat_inode_private_t *priv = (exfat_inode_private_t*)inode->i_private;
    exfat_t *exfat = priv->exfat;
    
    if (offset >= inode->i_size) {
        *read = 0;
        return 0;
    }
    
    u32 to_read = size;
    if (offset + to_read > inode->i_size) {
        to_read = inode->i_size - offset;
    }
    
    u8 *buffer = (u8*)buf;
    u32 done = 0;
    u32 cluster = priv->first_cluster;
    u32 cluster_size = exfat->bytes_per_cluster;
    
    //Skip clusters until offset
    u32 skip_clusters = offset / cluster_size;
    for (u32 i = 0; i < skip_clusters && cluster >= 2; i++) {
        cluster = exfat_next_cluster(exfat, cluster);
    }
    
    if (cluster < 2) {
        *read = 0;
        return -1;
    }
    
    //Reading
    u32 cluster_offset = offset % cluster_size;
    u8 temp_buf[cluster_size];
    
    while (done < to_read && cluster >= 2 && cluster != EXFAT_FAT_END) {
        exfat_read_cluster(exfat, cluster, temp_buf);
        
        u32 copy_start = cluster_offset;
        u32 copy_size = cluster_size - cluster_offset;
        if (copy_size > to_read - done) copy_size = to_read - done;
        
        memcpy(buffer + done, temp_buf + copy_start, copy_size);
        
        done += copy_size;
        cluster_offset = 0;
        cluster = exfat_next_cluster(exfat, cluster);
    }
    
    *read = done;
    return 0;
}

//Write a file
static int exfat_write(vfs_inode_t *inode, u64 offset, const void *buf,
                       u32 size, u32 *written) {
    exfat_inode_private_t *priv = (exfat_inode_private_t*)inode->i_private;
    exfat_t *exfat = priv->exfat;
    
    *written = 0;
    
    if (size == 0) return 0;
    
    u32 cluster_size = exfat->bytes_per_cluster;
    u8 *buffer = (u8*)buf;
    u32 to_write = size;
    u32 done = 0;
    
    //Determining how many clusters are needed
    u32 first_cluster = offset / cluster_size;
    u32 last_cluster = (offset + size - 1) / cluster_size;
    u32 clusters_needed = last_cluster - first_cluster + 1;
    
    //We get a list of clusters
    u32 *clusters = (u32*)malloc(clusters_needed * sizeof(u32));
    if (!clusters) return -1;
    
    //We go through existing clusters
    u32 cluster = priv->first_cluster;
    u32 cluster_idx = 0;
    
    //We skip to the desired cluster
    for (u32 i = 0; i < first_cluster && cluster >= 2; i++) {
        cluster = exfat_next_cluster(exfat, cluster);
    }
    
    //Collecting clusters
    u32 c = 0;
    while (c < clusters_needed) {
        if (cluster < 2 || cluster == EXFAT_FAT_END) {
            //Need a new cluster
            u32 new_cluster = 0;
            for (u32 i = 2; i < exfat->fat_entries; i++) {
                if (exfat->fat_cache[i] == EXFAT_FAT_FREE) {
                    new_cluster = i;
                    break;
                }
            }
            if (new_cluster == 0) {
                free(clusters);
                return -1;  //no room
            }
            
            //We connect
            if (c == 0 && cluster_idx == 0) {
                //This is the first cluster of the file
                priv->first_cluster = new_cluster;
            } else {
                //Linking with the previous one
                exfat->fat_cache[clusters[c-1]] = new_cluster;
            }
            exfat->fat_cache[new_cluster] = EXFAT_FAT_END;
            clusters[c] = new_cluster;
        } else {
            clusters[c] = cluster;
            cluster = exfat_next_cluster(exfat, cluster);
        }
        c++;
    }
    
    //Recording data
    for (u32 i = 0; i < clusters_needed; i++) {
        u32 current_cluster = clusters[i];
        u32 cluster_offset = (i == 0) ? (offset % cluster_size) : 0;
        u32 write_size = cluster_size - cluster_offset;
        if (write_size > to_write - done) write_size = to_write - done;
        
        if (write_size == cluster_size) {
            //Writing a whole cluster
            exfat_write_cluster(exfat, current_cluster, buffer + done);
        } else {
            //Read, modify, write
            u8 temp_buf[cluster_size];
            exfat_read_cluster(exfat, current_cluster, temp_buf);
            memcpy(temp_buf + cluster_offset, buffer + done, write_size);
            exfat_write_cluster(exfat, current_cluster, temp_buf);
        }
        
        done += write_size;
    }
    
    //Updating the file size
    if (offset + done > inode->i_size) {
        inode->i_size = offset + done;
        
        //Update the stream entry in the directory
        u8 dir_buf[cluster_size];
        exfat_read_cluster(exfat, priv->dir_cluster, dir_buf);
        
        u8 *ptr = dir_buf + priv->dir_entry * 32;
        exfat_stream_entry_t *stream = (exfat_stream_entry_t*)(ptr + 32);
        stream->data_length = inode->i_size;
        stream->valid_data_length = inode->i_size;
        
        exfat_write_cluster(exfat, priv->dir_cluster, dir_buf);
    }
    
    free(clusters);
    *written = done;
    exfat->fat_cache[0] |= 1;  // dirty
    
    return 0;
}

//Resizing
static int exfat_truncate(vfs_inode_t *inode, u64 new_size) {
    //TODO: implement truncate
    return -1;
}

//Synchronization
static int exfat_sync(vfs_inode_t *inode) {
    exfat_inode_private_t *priv = (exfat_inode_private_t*)inode->i_private;
    exfat_t *exfat = priv->exfat;
    
    if (exfat->fat_cache[0] & 1) {  // dirty
        //Write FAT back to disk
        for (u32 i = 0; i < exfat->vbr.fat_length; i++) {
            u64 sector = exfat->vbr.fat_offset + i;
            blockdev_write(exfat->dev, sector, exfat->bytes_per_sector / 512,
                          (u8*)exfat->fat_cache + i * exfat->bytes_per_sector);
        }
        exfat->fat_cache[0] &= ~1;  // clear dirty
    }
    
    return 0;
}

static int exfat_chmod(vfs_inode_t *inode, u32 mode) {
    if (!inode) return -1;
    
    exfat_inode_private_t *priv = (exfat_inode_private_t*)inode->i_private;
    exfat_t *exfat = priv->exfat;
    
    //Converting UNIX-like rights to exFAT attributes
    u16 attributes = 0;
    
    //File type
    if (inode->i_mode == FT_DIR) {
        attributes |= EXFAT_ATTR_DIRECTORY;
    }
    
    //Write permissions (for owner)
    if (!(mode & 0200)) {  //No write permission for owner
        attributes |= EXFAT_ATTR_READ_ONLY;
    }
    
    //We read the directory cluster where the file entry is located
    u8 buf[exfat->bytes_per_cluster];
    exfat_read_cluster(exfat, priv->dir_cluster, buf);
    
    //Finding the file entry
    u8 *ptr = buf + priv->dir_entry * 32;
    exfat_file_entry_t *file_entry = (exfat_file_entry_t*)ptr;
    
    //Updating attributes
    file_entry->file_attributes = attributes;
    
    //Write it back
    exfat_write_cluster(exfat, priv->dir_cluster, buf);
    
    //Updating the mode in the inode
    inode->i_mode = mode;
    
    return 0;
}

//Statistics
static int exfat_stat(vfs_inode_t *inode, void *stat_buf) {
    vfs_stat_t *stat = (vfs_stat_t*)stat_buf;
    return vfs_stat(inode, stat);
}

static int exfat_readdir(vfs_inode_t *dir, u64 *pos, char *name, 
                         u32 *name_len, u32 *type) {
    //Argument checking
    if (!dir || !pos || !name || !name_len || !type) return -1;
    if (dir->i_mode != FT_DIR) return -1;
    
    exfat_inode_private_t *priv = (exfat_inode_private_t*)dir->i_private;
    exfat_t *exfat = priv->exfat;
    
    //Current position = record number
    u32 entry_index = *pos;
    u32 entries_per_cluster = exfat->bytes_per_cluster / 32;
    
    //What cluster and its offset
    u32 cluster_index = entry_index / entries_per_cluster;
    u32 cluster_offset = entry_index % entries_per_cluster;
    
    //Finding the right cluster
    u32 cluster = priv->first_cluster;
    for (u32 i = 0; i < cluster_index; i++) {
        if (cluster < 2 || cluster == EXFAT_FAT_END) {
            *pos = 0;
            return -1;
        }
        cluster = exfat_next_cluster(exfat, cluster);
    }
    
    if (cluster < 2 || cluster == EXFAT_FAT_END) {
        *pos = 0;
        return -1;
    }
    
    //Reading the cluster
    u8 buf[exfat->bytes_per_cluster];
    if (exfat_read_cluster(exfat, cluster, buf) != 0) {
        return -1;
    }
    
    //We are looking for an entry starting with cluster_offset
    for (u32 i = cluster_offset; i < entries_per_cluster; i++) {
        u8 *entry_ptr = buf + i * 32;
        u8 entry_type = entry_ptr[0];
        
        //End of directory
        if (entry_type == 0) {
            *pos = 0;
            return -1;
        }
        
        //Is this a file or a directory?
        if (entry_type == 0x85) {  // EXFAT_ENTRY_FILE
            exfat_file_entry_t *file_entry = (exfat_file_entry_t*)entry_ptr;
            
            //We get the number of additional records
            u8 secondary_count = file_entry->secondary_count;
            
            //We are looking for stream entry (comes immediately after file entry)
            u8 *stream_ptr = entry_ptr + 32;
            exfat_stream_entry_t *stream = NULL;
            
            if (stream_ptr[0] == 0xC0) {  // EXFAT_ENTRY_STREAM
                stream = (exfat_stream_entry_t*)stream_ptr;
            }
            
            if (!stream) {
                //Skip all posts from this group
                i += secondary_count;
                continue;
            }
            
            //We are looking for name entry (comes after stream)
            u8 *name_ptr = stream_ptr + 32;
            int name_pos = 0;
            
            for (int j = 0; j < secondary_count - 1; j++) {
                if (name_ptr[0] == 0xC1) {  // EXFAT_ENTRY_NAME
                    exfat_name_entry_t *name_entry = (exfat_name_entry_t*)name_ptr;
                    
                    //Copy the name (ASCII characters only)
                    for (int k = 0; k < 15; k++) {
                        u16 c = name_entry->name[k];
                        if (c == 0) break;
                        if (c < 128 && name_pos < 255) {
                            name[name_pos++] = (char)c;
                        }
                    }
                }
                name_ptr += 32;
            }
            
            name[name_pos] = '\0';
            *name_len = name_pos;
            
            //Determining the type
            if (file_entry->file_attributes & 0x10) {  // EXFAT_ATTR_DIRECTORY
                *type = FT_DIR;
            } else {
                *type = FT_REG_FILE;
            }
            
            //Update the position to the NEXT record after this group
            *pos = entry_index + 1 + secondary_count;
            
            return 0;
        }
        
        //We skip auxiliary records (they will be processed together with the main one)
        if (entry_type == 0xC0 || entry_type == 0xC1) {
            continue;
        }
        
        //For unknown types we simply increase the counter
        entry_index++;
    }
    
    //If you got here, there are no more entries.
    *pos = 0;
    return -1;
}


static int exfat_parent(vfs_inode_t *inode, vfs_inode_t **parent) {
    if (!inode || !parent) return -1;
    
    exfat_inode_private_t *priv = (exfat_inode_private_t*)inode->i_private;
    exfat_t *exfat = priv->exfat;
    
    //If this is the root
    if (priv->first_cluster == exfat->root_cluster) {
        *parent = inode;  //The root refers to itself
        return 0;
    }
    
    //Create an inode for the parent
    vfs_inode_t *parent_inode = vfs_alloc_inode();
    exfat_inode_private_t *parent_priv = (exfat_inode_private_t*)malloc(sizeof(exfat_inode_private_t));
    
    parent_priv->exfat = exfat;
    parent_priv->first_cluster = priv->parent_cluster;
    parent_priv->data_length = 0;
    parent_priv->dir_cluster = 0;  //No need for parent
    parent_priv->dir_entry = 0;
    parent_priv->parent_cluster = exfat->root_cluster;  //Simplified
    
    parent_inode->i_mode = FT_DIR;
    parent_inode->i_size = 0;
    parent_inode->i_private = parent_priv;
    parent_inode->i_op = inode->i_op;
    parent_inode->i_fop = inode->i_fop;
    
    *parent = parent_inode;
    return 0;
}

//==================== MOUNTING ====================

static int exfat_unmount_fs(vfs_inode_t *root);

static vfs_operations_t exfat_i_op = {
    .lookup = exfat_lookup,
    .create = exfat_create,
    .unlink = exfat_unlink,
    .mkdir = exfat_mkdir,
    .rmdir = exfat_rmdir,
    .rename = exfat_rename,
    .chmod = exfat_chmod,
    .stat = exfat_stat,
    .readdir = exfat_readdir,
    .parent = exfat_parent,
    .unmount = exfat_unmount_fs,
    .get_name = exfat_get_name,
};

static vfs_file_operations_t exfat_f_op = {
    .read = exfat_read,
    .write = exfat_write,
    .truncate = exfat_truncate,
    .sync = exfat_sync,
};

static int exfat_mount(blockdev_t *dev, vfs_inode_t **root) {
    terminal_printf("[exFAT] Mounting...\n");
    
    exfat_t *exfat = (exfat_t*)malloc(sizeof(exfat_t));
    if (!exfat) return -1;
    
    memset(exfat, 0, sizeof(exfat_t));
    exfat->dev = dev;
    
    //Reading VBR (sector 0)
    u8 sector[512];
    if (exfat_read_sectors(exfat, 0, 1, sector) != 0) {
        free(exfat);
        return -1;
    }
    
    exfat_vbr_t *vbr = (exfat_vbr_t*)sector;
    
    //Checking the signature
    if (memcmp(vbr->fs_name, "EXFAT   ", 8) != 0) {
        terminal_error_printf("[exFAT] Not an exFAT volume (bad signature)\n");
        free(exfat);
        return -1;
    }
    
    exfat->vbr = *vbr;
    exfat->bytes_per_sector = 1 << vbr->bytes_per_sector_shift;
    exfat->sectors_per_cluster = 1 << vbr->sectors_per_cluster_shift;
    exfat->bytes_per_cluster = exfat->bytes_per_sector * exfat->sectors_per_cluster;
    
    exfat->fat_start = (u64)vbr->fat_offset * exfat->bytes_per_sector;
    exfat->cluster_heap_start = (u64)vbr->cluster_heap_offset * exfat->bytes_per_sector;
    exfat->root_cluster = vbr->root_dir_cluster;
    
    terminal_printf("[exFAT] Sector size: %d, Cluster size: %d, Root cluster: %d\n",
               exfat->bytes_per_sector, exfat->bytes_per_cluster, exfat->root_cluster);
    
    //Allocating memory for FAT
    u32 fat_size_bytes = vbr->fat_length * exfat->bytes_per_sector;
    exfat->fat_entries = fat_size_bytes / 4;
    exfat->fat_cache = (u32*)malloc(fat_size_bytes);
    if (!exfat->fat_cache) {
        free(exfat);
        return -1;
    }
    
    //Reading FAT
    for (u32 i = 0; i < vbr->fat_length; i++) {
        u64 sector_num = vbr->fat_offset + i;
        exfat_read_sectors(exfat, sector_num, 1, 
                          (u8*)exfat->fat_cache + i * exfat->bytes_per_sector);
    }
    
    //Create a root inode
    vfs_inode_t *root_inode = vfs_alloc_inode();
    exfat_inode_private_t *priv = (exfat_inode_private_t*)malloc(sizeof(exfat_inode_private_t));
    
    priv->exfat = exfat;
    priv->first_cluster = exfat->root_cluster;
    priv->data_length = 0;
    priv->dir_cluster = exfat->root_cluster;
    priv->dir_entry = 0;
    priv->parent_cluster = priv->first_cluster;
    
    root_inode->i_mode = FT_DIR;
    root_inode->i_uid = 0;
    root_inode->i_gid = 0;
    root_inode->i_size = 0;
    root_inode->i_ino = 2;
    root_inode->i_private = priv;
    
    root_inode->i_op = &exfat_i_op;
    root_inode->i_fop = &exfat_f_op;

    root_inode->i_dev = dev;
    strcpy(root_inode->i_fs_name, "exfat");
    
    *root = root_inode;
    
    terminal_printf("[exFAT] Mounted successfully\n");
    return 0;
}

static int exfat_unmount_fs(vfs_inode_t *root) {
    if (!root) return -1;
    
    exfat_inode_private_t *priv = (exfat_inode_private_t*)root->i_private;
    exfat_t *exfat = priv->exfat;
    
    //Write dirty FAT to disk
    if (exfat->fat_cache[0] & 1) {
        for (u32 i = 0; i < exfat->vbr.fat_length; i++) {
            u64 sector = exfat->vbr.fat_offset + i;
            blockdev_write(exfat->dev, sector, exfat->bytes_per_sector / 512,
                          (u8*)exfat->fat_cache + i * exfat->bytes_per_sector);
        }
        exfat->fat_cache[0] &= ~1;
    }
    
    //Freeing up memory
    free(exfat->fat_cache);
    free(exfat);
    free(priv);
    
    return 0;
}

//File system registration
static file_system_t exfat_fs = {
    .name = "exfat",
    .mount = exfat_mount,
    .unmount = exfat_unmount_fs,
    .next = NULL
};

void exfat_init(void) {
    vfs_register_fs(&exfat_fs);
    terminal_printf("[exFAT] Driver initialized\n");
}

int exfat_format(blockdev_t *dev) {
    if (!dev || dev->status != BLOCKDEV_READY) {
        terminal_error_printf("[exFAT] Device not ready\n");
        return -1;
    }
    
    terminal_printf("[exFAT] Formatting %s...\n", dev->name);
    
    //Defining the parameters
    u32 sector_size = dev->sector_size;
    u64 total_sectors = dev->total_sectors;
    
    //Select the cluster size (4KB - 32KB depending on the disk size)
    u8 sectors_per_cluster_shift;
    if (total_sectors < 0x100000) { //Less than 512MB
        sectors_per_cluster_shift = 0; //1 sector = 512 bytes
    } else if (total_sectors < 0x400000) { //Less than 2GB
        sectors_per_cluster_shift = 1; //2 sectors = 1KB
    } else if (total_sectors < 0x1000000) { //Less than 8GB
        sectors_per_cluster_shift = 3; //8 sectors = 4KB
    } else {
        sectors_per_cluster_shift = 6; //64 sectors = 32KB
    }
    
    u32 sectors_per_cluster = 1 << sectors_per_cluster_shift;
    u32 cluster_size = sector_size * sectors_per_cluster;
    
    //Calculate FAT size
    u64 total_clusters = total_sectors / sectors_per_cluster;
    u32 fat_entries = total_clusters + 2; //+2 for reserved
    u32 fat_sectors = (fat_entries * 4 + sector_size - 1) / sector_size;
    
    //Offsets
    u32 fat_offset = 24; //Sectors after VBR
    u32 cluster_heap_offset = fat_offset + fat_sectors;
    u32 root_cluster = 2; //First cluster
    
    terminal_printf("[exFAT] Total sectors: %lu, Cluster size: %u, FAT sectors: %u\n",
               total_sectors, cluster_size, fat_sectors);
    
    //1. Create and record VBR
    exfat_vbr_t vbr;
    memset(&vbr, 0, sizeof(vbr));
    
    // Jump boot
    vbr.jump_boot[0] = 0xEB;
    vbr.jump_boot[1] = 0x76;
    vbr.jump_boot[2] = 0x90;
    
    // FS Name
    memcpy(vbr.fs_name, "EXFAT   ", 8);
    
    // Partition info
    vbr.partition_offset = 0;
    vbr.volume_length = total_sectors;
    vbr.fat_offset = fat_offset;
    vbr.fat_length = fat_sectors;
    vbr.cluster_heap_offset = cluster_heap_offset;
    vbr.cluster_count = total_clusters;
    vbr.root_dir_cluster = root_cluster;
    vbr.volume_serial = 0x12345678; //TODO: random generation
    vbr.fs_revision = 0x0100; // Version 1.0
    vbr.volume_flags = 0;
    vbr.bytes_per_sector_shift = 9; // 512 bytes (2^9)
    vbr.sectors_per_cluster_shift = sectors_per_cluster_shift;
    vbr.number_of_fats = 1;
    vbr.drive_select = 0x80;
    vbr.percent_in_use = 0;
    
    // Boot code and signature
    vbr.boot_code[0] = 0xF4; // hlt
    vbr.signature = 0xAA55;
    
    //Write VBR to sector 0
    if (blockdev_write(dev, 0, 1, &vbr) != 0) {
        terminal_error_printf("[exFAT] Failed to write VBR\n");
        return -1;
    }
    
    //2. Create and write FAT
    u32 *fat = (u32*)malloc(fat_sectors * sector_size);
    if (!fat) return -1;
    
    memset(fat, 0, fat_sectors * sector_size);
    
    //Reserved Clusters
    fat[0] = 0xFFFFFFF8; // Media descriptor
    fat[1] = 0xFFFFFFFF; // EOC marker
    
    //Marking the root directory cluster
    fat[root_cluster] = 0xFFFFFFFF; // End of chain
    
    //Write FAT
    for (u32 i = 0; i < fat_sectors; i++) {
        if (blockdev_write(dev, fat_offset + i, 1, 
                          (u8*)fat + i * sector_size) != 0) {
            free(fat);
            terminal_error_printf("[exFAT] Failed to write FAT\n");
            return -1;
        }
    }
    free(fat);
    
    //3. Initialize the root directory
    u8 *cluster_buf = (u8*)malloc(cluster_size);
    if (!cluster_buf) return -1;
    
    memset(cluster_buf, 0, cluster_size);
    
    //Write down the root directory
    u64 root_sector = cluster_heap_offset + (root_cluster - 2) * sectors_per_cluster;
    if (blockdev_write(dev, root_sector, sectors_per_cluster, cluster_buf) != 0) {
        free(cluster_buf);
        terminal_error_printf("[exFAT] Failed to write root directory\n");
        return -1;
    }
    free(cluster_buf);
    
    terminal_printf("[exFAT] Format complete\n");
    return 0;
}

int exfat_get_name(vfs_inode_t *inode, char *name, int max_len) {
    if (!inode || !name) return -1;
    
    exfat_inode_private_t *priv = (exfat_inode_private_t*)inode->i_private;
    exfat_t *exfat = priv->exfat;
    
    //If this is the root
    if (priv->first_cluster == exfat->root_cluster) {
        strcpy(name, "");
        return 0;
    }
    
    //Checking the validity
    if (priv->dir_cluster == 0) {
        return -1;
    }
    
    //Reading the directory
    u8 buf[exfat->bytes_per_cluster];
    if (exfat_read_cluster(exfat, priv->dir_cluster, buf) != 0) {
        return -1;
    }
    
    //Finding the entry
    u8 *ptr = buf + priv->dir_entry * 32;
    
    //Checking that this is a file entry
    if (ptr[0] != 0x85) {
        return -1;
    }
    
    exfat_file_entry_t *file = (exfat_file_entry_t*)ptr;
    u8 count = file->secondary_count;
    
    //Looking for name entry
    u8 *name_ptr = ptr + 32 + 32;  //Skip file and stream
    int name_pos = 0;
    
    for (int i = 0; i < count - 1 && name_pos < max_len - 1; i++) {
        if (name_ptr[0] == 0xC1) {
            exfat_name_entry_t *name_entry = (exfat_name_entry_t*)name_ptr;
            for (int j = 0; j < 15; j++) {
                u16 c = name_entry->name[j];
                if (c == 0) break;
                if (c < 128) name[name_pos++] = (char)c;
            }
        }
        name_ptr += 32;
    }
    
    name[name_pos] = '\0';
    return 0;
}
