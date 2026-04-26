#ifndef MB_H
#define MB_H

#include <mbstruct.h>
#include <kernel/types.h>

void multiboot2_init_info(multiboot2_info_t* info);

int multiboot2_parse(void* mb2_addr, multiboot2_info_t* info);

void multiboot2_free_info(multiboot2_info_t* info);

u64 multiboot2_get_total_memory(multiboot2_info_t *info);
u32 multiboot2_get_memory_regions(multiboot2_info_t *info, memory_region_t *regions, u32 max_regions);
u64 multiboot2_get_usable_memory(multiboot2_info_t *info);
u8 multiboot2_get_boot_disk(multiboot2_info_t *info);
u32 multiboot2_get_boot_partition(multiboot2_info_t *info);
char* get_cmdline(void);
char* get_cmdline_param(const char *param);
char* get_root_uuid(void);

#endif
