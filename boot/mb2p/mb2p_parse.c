#include <mb.h>
#include <mbstruct.h>
#include <acpitables.h>

struct multiboot2_tag {
    u32 type;
    u32 size;
} __attribute__((packed));

struct multiboot2_tag_string {
    u32 type;
    u32 size;
    char string[];
} __attribute__((packed));

struct multiboot2_tag_basic_meminfo {
    u32 type;
    u32 size;
    u32 mem_lower;
    u32 mem_upper;
} __attribute__((packed));

struct multiboot2_tag_bootdev {
    u32 type;
    u32 size;
    u32 biosdev;
    u32 partition;
    u32 sub_partition;
} __attribute__((packed));

struct multiboot2_tag_mmap {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
    struct {
        u64 addr;
        u64 len;
        u32 type;
        u32 zero;
    } entries[];
} __attribute__((packed));

struct multiboot2_tag_module {
    u32 type;
    u32 size;
    u64 mod_start;
    u64 mod_end;
    char cmdline[];
} __attribute__((packed));

struct multiboot2_tag_framebuffer {
    u32 type;
    u32 size;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8 framebuffer_bpp;
    u8 framebuffer_type;
    u8 reserved;
    union {
        struct {
            u16 framebuffer_palette_num_colors;
            u8 framebuffer_palette[256][4];
        } palette;
        struct {
            u8 framebuffer_red_field_position;
            u8 framebuffer_red_mask_size;
            u8 framebuffer_green_field_position;
            u8 framebuffer_green_mask_size;
            u8 framebuffer_blue_field_position;
            u8 framebuffer_blue_mask_size;
        } color;
    } u;
} __attribute__((packed));

struct multiboot2_tag_acpi_v1 {
    u32 type;
    u32 size;
    rsdp_v1_t rsdp;
} __attribute__((packed));

struct multiboot2_tag_acpi_v2 {
    u32 type;
    u32 size;
    rsdp_v2_t rsdp;
} __attribute__((packed));

struct multiboot2_tag_efi64 {
    u32 type;
    u32 size;
    u64 pointer;
} __attribute__((packed));

struct multiboot2_tag_efi32 {
    u32 type;
    u32 size;
    u32 pointer;
} __attribute__((packed));

struct multiboot2_tag_efi_mmap {
    u32 type;
    u32 size;
    u32 desc_size;
    u32 desc_version;
    u8 efi_mmap[];
} __attribute__((packed));

struct multiboot2_tag_load_base_addr {
    u32 type;
    u32 size;
    u32 load_base_addr;
} __attribute__((packed));

static inline u32 align_up(u32 value, u32 alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

int multiboot2_parse(void* mb2_addr, multiboot2_info_t* info) {
    if (!mb2_addr || !info) return -1;
    u32 total_size = *(u32*)mb2_addr;

    struct multiboot2_tag* tag = (struct multiboot2_tag*)((u8*)mb2_addr + 8);

    while (tag->type != 0 && (u8*)tag < (u8*)mb2_addr + total_size) {
        switch (tag->type) {
            case 1: {  // Command line
                struct multiboot2_tag_string* t = (struct multiboot2_tag_string*)tag;
                info->cmdline = t->string;
                break;
            }
            
            case 2: {  // Boot loader name
                struct multiboot2_tag_string* t = (struct multiboot2_tag_string*)tag;
                info->boot_loader_name = t->string;
                break;
            }
            
            case 3: {  // Module
                struct multiboot2_tag_module* t = (struct multiboot2_tag_module*)tag;
                info->modules.count++;
                break;
            }
            
            case 4: {  // Basic meminfo
                struct multiboot2_tag_basic_meminfo* t = (struct multiboot2_tag_basic_meminfo*)tag;
                info->basic_meminfo.mem_lower = t->mem_lower;
                info->basic_meminfo.mem_upper = t->mem_upper;
                break;
            }
            
            case 5: {  // Boot device
                struct multiboot2_tag_bootdev* t = (struct multiboot2_tag_bootdev*)tag;
                info->bootdev.biosdev = t->biosdev;
                info->bootdev.partition = t->partition;
                info->bootdev.sub_partition = t->sub_partition;
                break;
            }
            
            case 6: {  // Memory map
                struct multiboot2_tag_mmap* t = (struct multiboot2_tag_mmap*)tag;
                info->mmap.entry_size = t->entry_size;

                u32 entries_data_size = tag->size - sizeof(struct multiboot2_tag_mmap) + 4;
                info->mmap.entry_count = entries_data_size / t->entry_size;

                void* entries_ptr = (void*)t->entries;
                info->mmap.entries = (void*)entries_ptr;
                break;
            }
            
            case 8: {  // Framebuffer
                struct multiboot2_tag_framebuffer* t = (struct multiboot2_tag_framebuffer*)tag;
                info->framebuffer.addr = t->framebuffer_addr;
                info->framebuffer.pitch = t->framebuffer_pitch;
                info->framebuffer.width = t->framebuffer_width;
                info->framebuffer.height = t->framebuffer_height;
                info->framebuffer.bpp = t->framebuffer_bpp;
                info->framebuffer.type = t->framebuffer_type;
                
                if (t->framebuffer_type == 1) {  // RGB
                    info->framebuffer.color_info.rgb.red_field_pos = t->u.color.framebuffer_red_field_position;
                    info->framebuffer.color_info.rgb.red_mask_size = t->u.color.framebuffer_red_mask_size;
                    info->framebuffer.color_info.rgb.green_field_pos = t->u.color.framebuffer_green_field_position;
                    info->framebuffer.color_info.rgb.green_mask_size = t->u.color.framebuffer_green_mask_size;
                    info->framebuffer.color_info.rgb.blue_field_pos = t->u.color.framebuffer_blue_field_position;
                    info->framebuffer.color_info.rgb.blue_mask_size = t->u.color.framebuffer_blue_mask_size;
                }
                break;
            }
            
            case 14: {  // Old ACPI (v1.0)
    	    	u64 phys_rsdp = (u64)mb2_addr + ((u8*)tag + 8 - (u8*)mb2_addr);
    		info->acpi.rsdpv1_addr = phys_rsdp;
    		break;
	    }

	    case 15: {  // New ACPI (v2.0+)
    	    	u64 phys_rsdp = (u64)mb2_addr + ((u8*)tag + 8 - (u8*)mb2_addr);
    		info->acpi.rsdpv2_addr = phys_rsdp;
		break;
	    }
            
            case 11: {  // EFI 32-bit
                struct multiboot2_tag_efi32* t = (struct multiboot2_tag_efi32*)tag;
                info->efi.system_table32 = t->pointer;
                break;
            }
            
            case 12: {  // EFI 64-bit
                struct multiboot2_tag_efi64* t = (struct multiboot2_tag_efi64*)tag;
                info->efi.system_table64 = t->pointer;
                break;
            }
            
            case 17: {  // EFI memory map
                struct multiboot2_tag_efi_mmap* t = (struct multiboot2_tag_efi_mmap*)tag;
                info->efi.mmap = t->efi_mmap;
                info->efi.mmap_size = tag->size - sizeof(struct multiboot2_tag_efi_mmap);
                info->efi.mmap_desc_size = t->desc_size;
                info->efi.mmap_desc_version = t->desc_version;
                break;
            }
            
            case 19: {  // EFI 32-bit image handle
                struct multiboot2_tag_efi32* t = (struct multiboot2_tag_efi32*)tag;
                info->efi.image_handle32 = (void*)(uptr)t->pointer;
                break;
            }
            
            case 20: {  // EFI 64-bit image handle
                struct multiboot2_tag_efi64* t = (struct multiboot2_tag_efi64*)tag;
                info->efi.image_handle64 = (void*)(uptr)t->pointer;
                break;
            }
            
            case 21: {  // Load base address
                struct multiboot2_tag_load_base_addr* t = (struct multiboot2_tag_load_base_addr*)tag;
                info->load_base_addr = t->load_base_addr;
                break;
            }
        }

        u32 next_tag_offset = align_up(tag->size, 8);
        tag = (struct multiboot2_tag*)((u8*)tag + next_tag_offset);
    }
    
    return 0;
}
