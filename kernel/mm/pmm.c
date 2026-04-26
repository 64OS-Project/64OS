#include <mb.h>
#include <mm/pmm.h>
#include <mbstruct.h>
#include <libk/string.h>
#include <kernel/types.h>
#include <kernel/terminal.h>

extern multiboot2_info_t mbinfo;

static u8 pmm_bitmap[64 * 1024 * 1024]; //64MB bitmap = 512GB memory
pmm_t g_pmm;

extern char _kernel_phys_start, _kernel_phys_end;

void pmm_init(pmm_t* pmm, u64 mb2_addr) {
    //1. Parsim MB2
    memory_region_t regions[64];
    int region_count = multiboot2_get_memory_regions(&mbinfo, regions, 64);
    
    //2. Find the LOWEST and HIGHEST address of available memory
    u64 lowest_addr = 0xFFFFFFFFFFFFFFFF;
    u64 highest_addr = 0;
    
    for (int i = 0; i < region_count; i++) {   
        if (regions[i].type == MULTIBOOT2_MMAP_AVAILABLE) {
            if (regions[i].base_addr < lowest_addr)
                lowest_addr = regions[i].base_addr;
            
            u64 region_end = regions[i].base_addr + regions[i].length;
            if (region_end > highest_addr)
                highest_addr = region_end;
        }
    }
    
    //3. Align to the page border
    if (lowest_addr & (PAGE_SIZE - 1))
        lowest_addr = (lowest_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    if (highest_addr & (PAGE_SIZE - 1))
        highest_addr = highest_addr & ~(PAGE_SIZE - 1);
    
    //4. Initialize the structure
    pmm->bitmap = pmm_bitmap;
    pmm->base_addr = lowest_addr;
    pmm->total_pages = (highest_addr - lowest_addr) / PAGE_SIZE;
    pmm->used_pages = 0;
    
    //5. Reset the bitmap
    u32 bitmap_bytes = (pmm->total_pages + 7) / 8;
    memset(pmm->bitmap, 0, bitmap_bytes);
    
    //6. WE RESERVE EVERYTHING THAT CANNOT BE TOUCHED
    //First we mark ALL pages as busy
    memset(pmm->bitmap, 0xFF, bitmap_bytes);
    pmm->used_pages = pmm->total_pages;
    
    //7. Free up ONLY available memory from MB2
    for (int i = 0; i < region_count; i++) {
        if (regions[i].type == MULTIBOOT2_MMAP_AVAILABLE) {
            u64 start = regions[i].base_addr;
            u64 end = regions[i].base_addr + regions[i].length;
            
            //Align
            start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            end = end & ~(PAGE_SIZE - 1);
            
            for (u64 addr = start; addr < end; addr += PAGE_SIZE) {
                if (addr >= lowest_addr && addr < highest_addr) {
                    u32 page_idx = (addr - pmm->base_addr) / PAGE_SIZE;
                    if (page_idx < pmm->total_pages) {
                        u32 byte_idx = page_idx / 8;
                        u32 bit = page_idx % 8;
                        pmm->bitmap[byte_idx] &= ~(1 << bit);
                        pmm->used_pages--;
                    }
                }
            }
        }
    }
    
    //8. We reserve MANDATORY areas:
    //- First 1MB (BIOS, boot.asm)
    //- The kernel itself (_kernel_phys_start - _kernel_phys_end)
    //- Multiboot2 structures
    //- PMM bitmap
    
    extern char _kernel_phys_start, _kernel_phys_end;
    
    //First 1MB
    for (u64 addr = 0; addr < 0x100000; addr += PAGE_SIZE) {
        if (addr >= lowest_addr && addr < highest_addr) {
            u32 page_idx = (addr - pmm->base_addr) / PAGE_SIZE;
            if (page_idx < pmm->total_pages) {
                u32 byte_idx = page_idx / 8;
                u32 bit = page_idx % 8;
                if (!(pmm->bitmap[byte_idx] & (1 << bit))) {
                    pmm->bitmap[byte_idx] |= (1 << bit);
                    pmm->used_pages++;
                }
            }
        }
    }
    
    //Core
    for (u64 addr = (u64)&_kernel_phys_start; 
         addr < (u64)&_kernel_phys_end; 
         addr += PAGE_SIZE) {
         if (addr >= lowest_addr && addr < highest_addr) {
            u32 page_idx = (addr - pmm->base_addr) / PAGE_SIZE;
            if (page_idx < pmm->total_pages) {
                u32 byte_idx = page_idx / 8;
                u32 bit = page_idx % 8;
                if (!(pmm->bitmap[byte_idx] & (1 << bit))) {
                    pmm->bitmap[byte_idx] |= (1 << bit);
                    pmm->used_pages++;
                }
            }
        }
    }
    
    //Multiboot2 structures
    u64 mb2_start = mb2_addr & ~(PAGE_SIZE - 1);
    u32 mb2_size = *(u32*)mb2_addr; //total_size at the beginning
    u64 mb2_end = (mb2_addr + mb2_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    for (u64 addr = mb2_start; addr < mb2_end; addr += PAGE_SIZE) {
        if (addr >= lowest_addr && addr < highest_addr) {
            u32 page_idx = (addr - pmm->base_addr) / PAGE_SIZE;
            if (page_idx < pmm->total_pages) {
                u32 byte_idx = page_idx / 8;
                u32 bit = page_idx % 8;
                if (!(pmm->bitmap[byte_idx] & (1 << bit))) {
                    pmm->bitmap[byte_idx] |= (1 << bit);
                    pmm->used_pages++;
                }
            }
        }
    }
    
    //PMM bitmap (self)
    u64 bitmap_start = (u64)pmm->bitmap;
    u64 bitmap_end = bitmap_start + bitmap_bytes;
    
    for (u64 addr = bitmap_start; addr < bitmap_end; addr += PAGE_SIZE) {
        if (addr >= lowest_addr && addr < highest_addr) {
            u32 page_idx = (addr - pmm->base_addr) / PAGE_SIZE;
            if (page_idx < pmm->total_pages) {
                u32 byte_idx = page_idx / 8;
                u32 bit = page_idx % 8;
                if (!(pmm->bitmap[byte_idx] & (1 << bit))) {
                    pmm->bitmap[byte_idx] |= (1 << bit);
                    pmm->used_pages++;
                }
            }
        }
    }
}

void* pmm_alloc_page(pmm_t* pmm) {
    if (!pmm || !pmm->bitmap) return NULL;
    
    u32 bitmap_bytes = (pmm->total_pages + 7) / 8;
    
    for (u32 byte_idx = 0; byte_idx < bitmap_bytes; byte_idx++) {
        if (pmm->bitmap[byte_idx] == 0x00) {
            //The entire byte is free - we are looking for the first bit
            pmm->bitmap[byte_idx] = 0x01;
            u32 page_idx = byte_idx * 8;
            
            if (page_idx < pmm->total_pages) {
                pmm->used_pages++;
                u64 addr = pmm->base_addr + (page_idx * PAGE_SIZE);
                return (void*)addr;
            }
        }
        
        if (pmm->bitmap[byte_idx] != 0xFF) {
            for (int bit = 0; bit < 8; bit++) {
                if (!(pmm->bitmap[byte_idx] & (1 << bit))) {
                    pmm->bitmap[byte_idx] |= (1 << bit);
                    u32 page_idx = byte_idx * 8 + bit;
                    
                    if (page_idx < pmm->total_pages) {
                        pmm->used_pages++;
                        u64 addr = pmm->base_addr + (page_idx * PAGE_SIZE);
                        return (void*)addr;
                    }
                }
            }
        }
    }
    
    // tio_printerr("[PMM] OUT OF MEMORY!\n");
    return NULL;
}

void pmm_free_page(pmm_t* pmm, void* addr) {
    if (!pmm || !pmm->bitmap || !addr) return;
    
    u64 page_addr = (u64)addr & ~(PAGE_SIZE - 1);
    
    if (page_addr < pmm->base_addr) return;
    
    u32 page_idx = (page_addr - pmm->base_addr) / PAGE_SIZE;
    if (page_idx >= pmm->total_pages) return;
    
    u32 byte_idx = page_idx / 8;
    u32 bit = page_idx % 8;
    
    if (pmm->bitmap[byte_idx] & (1 << bit)) {
        pmm->bitmap[byte_idx] &= ~(1 << bit);
        pmm->used_pages--;
    }
}

static u32 pmm_find_continuous_pages(pmm_t *pmm, u32 count) {
    if (!pmm || count == 0 || count > pmm->total_pages) return (u32)-1;
    
    u32 bitmap_bytes = (pmm->total_pages + 7) / 8;
    u32 continuous = 0;
    u32 start_page = 0;
    
    for (u32 byte_idx = 0; byte_idx < bitmap_bytes; byte_idx++) {
        if (pmm->bitmap[byte_idx] == 0xFF) {
            // Все биты в байте заняты, сбрасываем счетчик
            continuous = 0;
            continue;
        }
        
        // Проверяем каждый бит в байте
        for (int bit = 0; bit < 8; bit++) {
            u32 page_idx = byte_idx * 8 + bit;
            if (page_idx >= pmm->total_pages) break;
            
            if (!(pmm->bitmap[byte_idx] & (1 << bit))) {
                // Страница свободна
                if (continuous == 0) {
                    start_page = page_idx;
                }
                continuous++;
                
                if (continuous == count) {
                    return start_page;
                }
            } else {
                // Страница занята, сбрасываем счетчик
                continuous = 0;
            }
        }
    }
    
    return (u32)-1;
}

static void pmm_mark_range(pmm_t *pmm, u32 start_page, u32 count, bool used) {
    for (u32 i = 0; i < count; i++) {
        u32 page_idx = start_page + i;
        if (page_idx >= pmm->total_pages) break;
        
        u32 byte_idx = page_idx / 8;
        u32 bit = page_idx % 8;
        
        if (used) {
            if (!(pmm->bitmap[byte_idx] & (1 << bit))) {
                pmm->bitmap[byte_idx] |= (1 << bit);
                pmm->used_pages++;
            }
        } else {
            if (pmm->bitmap[byte_idx] & (1 << bit)) {
                pmm->bitmap[byte_idx] &= ~(1 << bit);
                pmm->used_pages--;
            }
        }
    }
}

void* pmm_alloc_range(pmm_t *pmm, u32 page_count) {
    if (!pmm || page_count == 0) return NULL;
    
    u32 start_page = pmm_find_continuous_pages(pmm, page_count);
    if (start_page == (u32)-1) {
        terminal_error_printf("[PMM] Failed to allocate %u continuous pages\n", page_count);
        return NULL;
    }
    
    pmm_mark_range(pmm, start_page, page_count, true);
    
    u64 addr = pmm->base_addr + (start_page * PAGE_SIZE);
    
    // Обнуляем выделенную память (важно для безопасности)
    void *virt_addr = (void*)(uptr)addr;
    memset(virt_addr, 0, page_count * PAGE_SIZE);
    
    terminal_debug_printf("[PMM] Allocated %u pages at phys=0x%llx\n", 
                          page_count, addr);
    return virt_addr;
}

void pmm_free_range(pmm_t *pmm, void *addr, u32 page_count) {
    if (!pmm || !addr || page_count == 0) return;
    
    u64 page_addr = (u64)addr & ~(PAGE_SIZE - 1);
    if (page_addr < pmm->base_addr) return;
    
    u32 start_page = (page_addr - pmm->base_addr) / PAGE_SIZE;
    if (start_page >= pmm->total_pages) return;
    
    // Проверяем, что все страницы действительно были выделены
    for (u32 i = 0; i < page_count; i++) {
        u32 page_idx = start_page + i;
        if (page_idx >= pmm->total_pages) break;
        
        u32 byte_idx = page_idx / 8;
        u32 bit = page_idx % 8;
        
        if (!(pmm->bitmap[byte_idx] & (1 << bit))) {
            terminal_warn_printf("[PMM] Warning: freeing already free page %u\n", page_idx);
        }
    }
    
    pmm_mark_range(pmm, start_page, page_count, false);
    
    terminal_debug_printf("[PMM] Freed %u pages at phys=0x%llx\n", 
                          page_count, page_addr);
}

void* pmm_alloc_aligned_range(pmm_t *pmm, u32 page_count, u32 alignment_pages) {
    if (!pmm || page_count == 0 || alignment_pages == 0) return NULL;
    
    // alignment_pages должно быть степенью двойки
    if ((alignment_pages & (alignment_pages - 1)) != 0) return NULL;
    
    u32 bitmap_bytes = (pmm->total_pages + 7) / 8;
    u32 continuous = 0;
    u32 start_page = 0;
    bool in_aligned = false;
    
    for (u32 byte_idx = 0; byte_idx < bitmap_bytes; byte_idx++) {
        if (pmm->bitmap[byte_idx] == 0xFF) {
            continuous = 0;
            in_aligned = false;
            continue;
        }
        
        for (int bit = 0; bit < 8; bit++) {
            u32 page_idx = byte_idx * 8 + bit;
            if (page_idx >= pmm->total_pages) break;
            
            // Проверка выравнивания для начала блока
            if (!in_aligned && (page_idx % alignment_pages) != 0) {
                continue;
            }
            
            if (!(pmm->bitmap[byte_idx] & (1 << bit))) {
                if (continuous == 0) {
                    start_page = page_idx;
                    in_aligned = true;
                }
                continuous++;
                
                if (continuous == page_count) {
                    pmm_mark_range(pmm, start_page, page_count, true);
                    u64 addr = pmm->base_addr + (start_page * PAGE_SIZE);
                    void *virt_addr = (void*)(uptr)addr;
                    memset(virt_addr, 0, page_count * PAGE_SIZE);
                    return virt_addr;
                }
            } else {
                continuous = 0;
                in_aligned = false;
            }
        }
    }
    
    terminal_error_printf("[PMM] Failed to allocate aligned %u pages (align=%u)\n", 
                          page_count, alignment_pages);
    return NULL;
}
