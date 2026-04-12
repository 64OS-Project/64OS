// fs/procfs/procfs.c
#include <fs/procfs.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <kernel/timer.h>
#include <kernel/sched.h>
#include <mm/pmm.h>
#include <kernel/partition.h>
#include <acpi.h>
#include <asm/cpu.h>
#include <mb.h>

//=============================================================================
//Structures
//=============================================================================

typedef struct procfs_entry {
    char name[64];
    int type;  //FT_DIR or FT_REG_FILE
    char *(*read)(void);
    struct procfs_entry *children;
    struct procfs_entry *next;
    struct procfs_entry *parent;
} procfs_entry_t;

static procfs_entry_t *procfs_root_entry = NULL;

//=============================================================================
//Content Generators
//=============================================================================

static char *proc_read_version(void) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "64OS version 1.0.0\n"
             "Kernel: 64-bit x86_64\n"
             "Build: " __DATE__ " " __TIME__ "\n");
    return buffer;
}

static char *proc_read_cpuinfo(void) {
    static char buffer[1024];
    u32 eax, ebx, ecx, edx;
    char vendor[13] = {0};
    char *ptr = buffer;
    int remaining = sizeof(buffer);
    
    cpuid(0, &eax, &ebx, &ecx, &edx);
    *(u32*)vendor = ebx;
    *(u32*)(vendor + 4) = edx;
    *(u32*)(vendor + 8) = ecx;
    
    ptr += snprintf(ptr, remaining, "vendor_id\t: %s\n", vendor);
    
    cpuid(1, &eax, &ebx, &ecx, &edx);
    u32 family = (eax >> 8) & 0xF;
    u32 model = (eax >> 4) & 0xF;
    u32 stepping = eax & 0xF;
    
    ptr += snprintf(ptr, remaining, "cpu family\t: %u\n", family);
    ptr += snprintf(ptr, remaining, "model\t\t: %u\n", model);
    ptr += snprintf(ptr, remaining, "stepping\t: %u\n", stepping);
    
    ptr += snprintf(ptr, remaining, "flags\t\t:");
    if (edx & (1 << 23)) ptr += snprintf(ptr, remaining, " mmx");
    if (edx & (1 << 25)) ptr += snprintf(ptr, remaining, " sse");
    if (edx & (1 << 26)) ptr += snprintf(ptr, remaining, " sse2");
    if (ecx & (1 << 0)) ptr += snprintf(ptr, remaining, " sse3");
    if (ecx & (1 << 9)) ptr += snprintf(ptr, remaining, " ssse3");
    if (ecx & (1 << 19)) ptr += snprintf(ptr, remaining, " sse4_1");
    if (ecx & (1 << 20)) ptr += snprintf(ptr, remaining, " sse4_2");
    ptr += snprintf(ptr, remaining, "\n");
    
    cpuid(0xB, &eax, &ebx, &ecx, &edx);
    u32 cores = ebx & 0xFFFF;
    ptr += snprintf(ptr, remaining, "cpu cores\t: %u\n", cores);
    
    u64 tsc_freq = timer_tsc_freq();
    ptr += snprintf(ptr, remaining, "cpu MHz\t\t: %u.%03u\n",
                   (u32)(tsc_freq / 1000000), (u32)((tsc_freq % 1000000) / 1000));
    
    return buffer;
}

static char *proc_read_meminfo(void) {
    static char buffer[512];
    extern pmm_t pmm;
    
    u64 total_mb = ((u64)pmm.total_pages * PAGE_SIZE) / (1024 * 1024);
    u64 free_mb = ((u64)pmm_get_free_pages(&pmm) * PAGE_SIZE) / (1024 * 1024);
    u64 used_mb = total_mb - free_mb;
    
    snprintf(buffer, sizeof(buffer),
             "MemTotal:        %llu MB\n"
             "MemFree:         %llu MB\n"
             "MemUsed:         %llu MB\n"
             "PageSize:        %u KB\n"
             "TotalPages:      %u\n"
             "UsedPages:       %u\n"
             "FreePages:       %u\n",
             total_mb, free_mb, used_mb,
             PAGE_SIZE / 1024,
             pmm.total_pages,
             pmm.used_pages,
             pmm_get_free_pages(&pmm));
    
    return buffer;
}

static char *proc_read_uptime(void) {
    static char buffer[64];
    u64 ticks = timer_apic_ticks();
    u32 seconds = ticks / 1000;
    u32 hours = seconds / 3600;
    u32 minutes = (seconds % 3600) / 60;
    u32 secs = seconds % 60;
    
    snprintf(buffer, sizeof(buffer), "%u:%02u:%02u\n", hours, minutes, secs);
    return buffer;
}

static char *proc_read_loadavg(void) {
    static char buffer[64];
    task_info_t tasks[64];
    int count = task_list(tasks, 64);
    int running = 0;
    
    for (int i = 0; i < count; i++) {
        if (tasks[i].state == TASK_RUNNING || tasks[i].state == TASK_READY) {
            running++;
        }
    }
    
    snprintf(buffer, sizeof(buffer), "%.2f %.2f %.2f %d/%d\n",
             (float)running, (float)running, (float)running, running, count);
    return buffer;
}

static char *proc_read_cmdline(void) {
    static char buffer[256];
    char *cmdline = get_cmdline();
    if (cmdline && cmdline[0]) {
        strncpy(buffer, cmdline, sizeof(buffer) - 1);
    } else {
        strcpy(buffer, "none");
    }
    return buffer;
}

static char *proc_read_stat(void) {
    static char buffer[256];
    task_info_t tasks[64];
    int count = task_list(tasks, 64);
    int running = 0, sleeping = 0, zombie = 0;
    
    for (int i = 0; i < count; i++) {
        switch (tasks[i].state) {
            case TASK_RUNNING: running++; break;
            case TASK_READY: running++; break;
            case TASK_BLOCKED: sleeping++; break;
            case TASK_ZOMBIE: zombie++; break;
        }
    }
    
    snprintf(buffer, sizeof(buffer),
             "processes %d\n"
             "procs_running %d\n"
             "procs_blocked %d\n"
             "zombies %d\n",
             count, running, sleeping, zombie);
    return buffer;
}

static char *proc_read_partitions(void) {
    static char buffer[2048];
    char *ptr = buffer;
    int remaining = sizeof(buffer);
    
    blockdev_t *disks[32];
    int disk_count = blockdev_get_list(disks, 32);
    
    for (int i = 0; i < disk_count; i++) {
        ptr += snprintf(ptr, remaining, "%s: %llu MB (%llu sectors)\n",
                       disks[i]->name,
                       disks[i]->total_bytes / (1024 * 1024),
                       disks[i]->total_sectors);
        
        //Sections
        partition_t *part = NULL;
        for (int j = 0; j < 32; j++) {
            part = partition_get_by_index(j);
            if (part && part->parent_disk == disks[i]) {
                ptr += snprintf(ptr, remaining, "  %s: %llu MB (LBA %llu, type %s)\n",
                               part->name,
                               partition_get_size_mb(part),
                               part->start_lba,
                               partition_type_to_string(part));
            }
        }
    }
    
    return buffer;
}

static char *proc_read_mounts(void) {
    static char buffer[1024];
    char *ptr = buffer;
    int remaining = sizeof(buffer);
    
    mount_entry_t entries[32];
    int count = vfs_get_mount_points("/", entries, 32);
    
    for (int i = 0; i < count; i++) {
        const char *fs_name = "unknown";
        if (entries[i].inode && entries[i].inode->i_fs_name[0]) {
            fs_name = entries[i].inode->i_fs_name;
        }
        ptr += snprintf(ptr, remaining, "%s on %s type %s (rw,relatime)\n",
                       entries[i].inode->i_dev ? entries[i].inode->i_dev->name : "none",
                       entries[i].name, fs_name);
    }
    
    return buffer;
}

static char *proc_read_filesystems(void) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "nodev   procfs\n"
             "nodev   devfs\n"
             "        exfat\n"
             "        fat32\n");
    return buffer;
}

static char *proc_read_devices(void) {
    static char buffer[512];
    char *ptr = buffer;
    int remaining = sizeof(buffer);
    
    ptr += snprintf(ptr, remaining, "Block devices:\n");
    
    blockdev_t *disks[32];
    int disk_count = blockdev_get_list(disks, 32);
    
    for (int i = 0; i < disk_count; i++) {
        const char *type = (disks[i]->type == BLOCKDEV_TYPE_IDE) ? "ide" : "sata";
        ptr += snprintf(ptr, remaining, "  %s (%s)\n", disks[i]->name, type);
    }
    
    ptr += snprintf(ptr, remaining, "\nCharacter devices:\n");
    ptr += snprintf(ptr, remaining, "  stdin\n");
    ptr += snprintf(ptr, remaining, "  stdout\n");
    ptr += snprintf(ptr, remaining, "  stderr\n");
    
    return buffer;
}

//=============================================================================
//VFS operations
//=============================================================================

typedef struct procfs_file {
    char *(*read_func)(void);
} procfs_file_t;

static int procfs_read(vfs_inode_t *inode, u64 offset, void *buf,
                       u32 size, u32 *read) {
    if (!inode || !buf || !read) return -1;
    
    procfs_file_t *file = (procfs_file_t*)inode->i_private;
    if (!file || !file->read_func) return -1;
    
    if (offset > 0) {
        *read = 0;
        return 0;
    }
    
    char *content = file->read_func();
    if (!content) return -1;
    
    sz len = strlen(content);
    if (size < len) len = size;
    
    memcpy(buf, content, len);
    *read = len;
    
    return 0;
}

static int procfs_readdir(vfs_inode_t *dir, u64 *pos, char *name,
                          u32 *name_len, u32 *type) {
    if (!dir || !pos || !name || !name_len || !type) return -1;
    
    procfs_entry_t *entry = (procfs_entry_t*)dir->i_private;
    if (!entry) return -1;
    
    int idx = 0;
    procfs_entry_t *child = entry->children;
    
    while (child && idx < *pos) {
        child = child->next;
        idx++;
    }
    
    if (!child) return -1;
    
    strcpy(name, child->name);
    *name_len = strlen(child->name);
    *type = child->type;
    (*pos)++;
    
    return 0;
}

static int procfs_lookup(vfs_inode_t *dir, const char *name, vfs_inode_t **result) {
    if (!dir || !name || !result) return -1;
    
    procfs_entry_t *parent = (procfs_entry_t*)dir->i_private;
    if (!parent) return -1;
    
    for (procfs_entry_t *child = parent->children; child; child = child->next) {
        if (strcmp(child->name, name) == 0) {
            vfs_inode_t *inode = vfs_alloc_inode();
            if (!inode) return -1;
            
            if (child->type == FT_DIR) {
                inode->i_mode = FT_DIR;
                inode->i_private = child;
            } else {
                inode->i_mode = FT_REG_FILE;
                procfs_file_t *file = (procfs_file_t*)malloc(sizeof(procfs_file_t));
                file->read_func = child->read;
                inode->i_private = file;
            }
            
            inode->i_op = dir->i_op;
            inode->i_fop = dir->i_fop;
            *result = inode;
            return 0;
        }
    }
    
    return -1;
}

static int procfs_get_name(vfs_inode_t *inode, char *name, int max_len) {
    if (!inode || !name) return -1;
    
    procfs_entry_t *entry = (procfs_entry_t*)inode->i_private;
    if (!entry) return -1;
    
    strncpy(name, entry->name, max_len - 1);
    return 0;
}

static int procfs_parent(vfs_inode_t *inode, vfs_inode_t **parent) {
    if (!inode || !parent) return -1;
    
    procfs_entry_t *entry = (procfs_entry_t*)inode->i_private;
    if (!entry || !entry->parent) {
        *parent = procfs_root;
        vfs_inode_ref(*parent);
        return 0;
    }
    
    *parent = procfs_root;
    vfs_inode_ref(*parent);
    return 0;
}

static vfs_operations_t procfs_i_op = {
    .lookup = procfs_lookup,
    .readdir = procfs_readdir,
    .get_name = procfs_get_name,
    .parent = procfs_parent,
};

static vfs_file_operations_t procfs_f_op = {
    .read = procfs_read,
};

//=============================================================================
//Wood assembly
//=============================================================================

static void add_proc_entry(procfs_entry_t *parent, const char *name, int type,
                           char *(*read_func)(void)) {
    procfs_entry_t *entry = (procfs_entry_t*)malloc(sizeof(procfs_entry_t));
    strncpy(entry->name, name, 63);
    entry->type = type;
    entry->read = read_func;
    entry->children = NULL;
    entry->parent = parent;
    entry->next = parent->children;
    parent->children = entry;
}

static void build_procfs_tree(void) {
    procfs_root_entry = (procfs_entry_t*)malloc(sizeof(procfs_entry_t));
    strcpy(procfs_root_entry->name, "proc");
    procfs_root_entry->type = FT_DIR;
    procfs_root_entry->read = NULL;
    procfs_root_entry->children = NULL;
    procfs_root_entry->parent = NULL;
    
    add_proc_entry(procfs_root_entry, "version", FT_REG_FILE, proc_read_version);
    add_proc_entry(procfs_root_entry, "cpuinfo", FT_REG_FILE, proc_read_cpuinfo);
    add_proc_entry(procfs_root_entry, "meminfo", FT_REG_FILE, proc_read_meminfo);
    add_proc_entry(procfs_root_entry, "uptime", FT_REG_FILE, proc_read_uptime);
    add_proc_entry(procfs_root_entry, "loadavg", FT_REG_FILE, proc_read_loadavg);
    add_proc_entry(procfs_root_entry, "cmdline", FT_REG_FILE, proc_read_cmdline);
    add_proc_entry(procfs_root_entry, "stat", FT_REG_FILE, proc_read_stat);
    add_proc_entry(procfs_root_entry, "partitions", FT_REG_FILE, proc_read_partitions);
    add_proc_entry(procfs_root_entry, "mounts", FT_REG_FILE, proc_read_mounts);
    add_proc_entry(procfs_root_entry, "filesystems", FT_REG_FILE, proc_read_filesystems);
    add_proc_entry(procfs_root_entry, "devices", FT_REG_FILE, proc_read_devices);
}

//=============================================================================
//Initialization
//=============================================================================

vfs_inode_t *procfs_root = NULL;

void procfs_init(void) {
    build_procfs_tree();
    
    procfs_root = vfs_alloc_inode();
    procfs_root->i_mode = FT_DIR;
    procfs_root->i_private = procfs_root_entry;
    procfs_root->i_op = &procfs_i_op;
    procfs_root->i_fop = &procfs_f_op;
    strcpy(procfs_root->i_fs_name, "procfs");
    
    vfs_mount_point("/proc", procfs_root);
    
    terminal_success_printf("[PROCFS] Mounted at /proc\n");
}
