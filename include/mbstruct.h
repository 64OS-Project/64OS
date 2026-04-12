#ifndef MBSTRUCT_H
#define MBSTRUCT_H

#include <kernel/types.h>

#define MULTIBOOT2_MMAP_AVAILABLE        1
#define MULTIBOOT2_MMAP_RESERVED         2
#define MULTIBOOT2_MMAP_ACPI_RECLAIMABLE 3
#define MULTIBOOT2_MMAP_ACPI_NVS         4
#define MULTIBOOT2_MMAP_BAD               5

#define MULTIBOOT2_FRAMEBUFFER_TYPE_INDEXED   0
#define MULTIBOOT2_FRAMEBUFFER_TYPE_RGB       1
#define MULTIBOOT2_FRAMEBUFFER_TYPE_EGA_TEXT  2

typedef struct {
    char* cmdline;
    char* boot_loader_name;
    struct {
        u32 mem_lower;
        u32 mem_upper;
    } basic_meminfo;
    struct {
        u32 biosdev;
        u32 partition;
        u32 sub_partition;
    } bootdev;
    struct {
        u32 entry_count;
        u32 entry_size;
        struct {
            u64 addr;
            u64 len;
            u32 type;
        } *entries;
    } mmap;
    struct {
        u64 addr;
        u32 pitch;
        u32 width;
        u32 height;
        u8 bpp;
        u8 type;
        union {
            struct {
                u8 red_field_pos;
                u8 red_mask_size;
                u8 green_field_pos;
                u8 green_mask_size;
                u8 blue_field_pos;
                u8 blue_mask_size;
            } rgb;
        } color_info;
    } framebuffer;
    struct {
        u32 count;
        struct {
            u64 start;
            u64 end;
            char* cmdline;
        } *modules;
    } modules;
    struct {
        u32 rsdpv1_addr;
        u32 rsdpv2_addr;
    } acpi;
    struct {
        u32 system_table32;
        u64 system_table64;
        void* mmap;
        u32 mmap_size;
        u32 mmap_desc_size;
        u32 mmap_desc_version;
        void* image_handle32;
        void* image_handle64;
    } efi;
    u64 load_base_addr;
    
} multiboot2_info_t;

typedef struct {
    u64 base_addr;
    u64 length;
    u32 type;
} memory_region_t;

#endif
