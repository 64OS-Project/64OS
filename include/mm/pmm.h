#ifndef PMM_H
#define PMM_H

#include <kernel/types.h>

// Page size - 4KB (standard for x86_64))
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#define PMM_BITMAP_MAX_SIZE (64 * 1024 * 1024)

// Physical memory manager structure
typedef struct {
    u8* bitmap;        // Pointer to a bit array
    u64 base_addr;     // Starting physical address
    u32 total_pages;   // Total pages under management
    u32 used_pages;    // Occupied pages
    u64 bitmap_size;   // Bitmap size in bytes
} pmm_t;

void pmm_init(pmm_t* pmm, u64 mb2_addr);

void* pmm_alloc_page(pmm_t* pmm);

void pmm_free_page(pmm_t* pmm, void* addr);

void* pmm_alloc_range(pmm_t *pmm, u32 page_count);

void pmm_free_range(pmm_t *pmm, void *addr, u32 page_count);

void* pmm_alloc_aligned_range(pmm_t *pmm, u32 page_count, u32 alignment_pages);

u32 pmm_get_free_continuous_max(pmm_t *pmm);

static inline u32 pmm_get_free_pages(pmm_t* pmm) {
    return pmm ? (pmm->total_pages - pmm->used_pages) : 0;
}

static inline u32 pmm_get_used_pages(pmm_t* pmm) {
    return pmm ? pmm->used_pages : 0;
}
  
#endif // PMM_H
