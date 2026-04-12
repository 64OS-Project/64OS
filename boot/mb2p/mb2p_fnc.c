#include <mb.h>
#include <mbstruct.h>
#include <libk/string.h>

struct multiboot2_tag {
    u32 type;
    u32 size;
} __attribute__((packed));

struct multiboot2_tag_module {
    u32 type;
    u32 size;
    u64 mod_start;
    u64 mod_end;
    char cmdline[];
} __attribute__((packed));

static inline u32 align_up(u32 value, u32 alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

void multiboot2_init_info(multiboot2_info_t* info) {
    if (!info) return;
    u8* ptr = (u8*)info;
    for (u32 i = 0; i < sizeof(multiboot2_info_t); i++) {
        ptr[i] = 0;
    }
    info->cmdline = 0;
    info->boot_loader_name = 0;
    info->mmap.entries = 0;
    info->modules.modules = 0;
    info->efi.mmap = 0;
}

void multiboot2_free_info(multiboot2_info_t* info) {
    if (!info) return;
    
    multiboot2_init_info(info);
}

int multiboot2_collect_modules(void* mb2_addr, multiboot2_info_t* info) {
    if (!mb2_addr || !info) return -1;
    info->modules.count = 0;
    
    u32 total_size = *(u32*)mb2_addr;
    struct multiboot2_tag* tag = (struct multiboot2_tag*)((u8*)mb2_addr + 8);
    
    while (tag->type != 0 && (u8*)tag < (u8*)mb2_addr + total_size) {
        if (tag->type == 3) {  // Module
            info->modules.count++;
        }
        
        u32 next_tag_offset = align_up(tag->size, 8);
        tag = (struct multiboot2_tag*)((u8*)tag + next_tag_offset);
    }
    
    return info->modules.count;
}

u64 multiboot2_get_total_memory(multiboot2_info_t *info) {
    if (!info) return 0;
    
    u64 total = 0;

    if (info->basic_meminfo.mem_upper > 0) {
        total = 1024 * 1024 + ((u64)info->basic_meminfo.mem_upper * 1024);
    }
    if (info->mmap.entries && info->mmap.entry_count > 0) {
        u64 mmap_total = 0;
        
        for (u32 i = 0; i < info->mmap.entry_count; i++) {
            if (info->mmap.entries[i].type == MULTIBOOT2_MMAP_AVAILABLE) {
                mmap_total += info->mmap.entries[i].len;
            }
        }
        if (mmap_total > total) {
            total = mmap_total;
        }
    }
    
    return total;
}

u32 multiboot2_get_memory_regions(multiboot2_info_t *info, memory_region_t *regions, u32 max_regions) {
    if (!info || !regions || max_regions == 0) return 0;
    
    u32 count = 0;

    if (info->mmap.entries && info->mmap.entry_count > 0) {
        for (u32 i = 0; i < info->mmap.entry_count && count < max_regions; i++) {
            regions[count].base_addr = info->mmap.entries[i].addr;
            regions[count].length = info->mmap.entries[i].len;
            regions[count].type = info->mmap.entries[i].type;
            count++;
        }
    } else {
        if (info->basic_meminfo.mem_lower > 0 && count < max_regions) {
            regions[count].base_addr = 0;
            regions[count].length = (u64)info->basic_meminfo.mem_lower * 1024;
            regions[count].type = MULTIBOOT2_MMAP_AVAILABLE;
            count++;
        }
        
        if (info->basic_meminfo.mem_upper > 0 && count < max_regions) {
            regions[count].base_addr = 1024 * 1024; /*
 * 1MB
 */
            regions[count].length = (u64)info->basic_meminfo.mem_upper * 1024;
            regions[count].type = MULTIBOOT2_MMAP_AVAILABLE;
            count++;
        }
    }
    
    return count;
}

u64 multiboot2_get_usable_memory(multiboot2_info_t *info) {
    if (!info) return 0;
    
    u64 total = 0;
    
    if (info->mmap.entries && info->mmap.entry_count > 0) {
        for (u32 i = 0; i < info->mmap.entry_count; i++) {
            if (info->mmap.entries[i].type == MULTIBOOT2_MMAP_AVAILABLE) {
                total += info->mmap.entries[i].len;
            }
        }
    } else {
        if (info->basic_meminfo.mem_lower > 0) {
            total += (u64)info->basic_meminfo.mem_lower * 1024;
        }
        if (info->basic_meminfo.mem_upper > 0) {
            total += (u64)info->basic_meminfo.mem_upper * 1024;
        }
        total += 1024 * 1024;
    }
    
    return total;
}

u8 multiboot2_get_boot_disk(multiboot2_info_t *info) {
    if (!info) return 0x80;  // default
    return (u8)(info->bootdev.biosdev & 0xFF);
}

u32 multiboot2_get_boot_partition(multiboot2_info_t *info) {
    if (!info) return 0xFFFFFFFF;
    return info->bootdev.partition;
}

char* get_cmdline(void) {
    extern multiboot2_info_t mbinfo;
    if (mbinfo.cmdline && mbinfo.cmdline[0] != '\0') {
        return mbinfo.cmdline;
    }
    return NULL;
}

char* get_cmdline_param(const char *param) {
    char *cmdline = get_cmdline();
    if (!cmdline) return NULL;
    
    //We are looking for a parameter (for example "root=")
    char search_str[64];
    snprintf(search_str, sizeof(search_str), "%s=", param);
    
    char *start = strstr(cmdline, search_str);
    if (!start) return NULL;
    
    start += strlen(search_str);
    
    //Allocate memory for the value (until space or end of line)
    static char value[256];
    int i = 0;
    while (start[i] && start[i] != ' ' && start[i] != '\t' && start[i] != '\n' && i < 255) {
        value[i] = start[i];
        i++;
    }
    value[i] = '\0';
    
    return value;
}

char* get_root_uuid(void) {
    char *root = get_cmdline_param("root");
    if (!root) return NULL;
    
    //Checking the format root=UUID=xxxxx
    if (strncmp(root, "UUID=", 5) == 0) {
        return root + 5;
    }
    
    return root;
}
