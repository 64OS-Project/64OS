#ifndef PAGING_H
#define PAGING_H

#include <kernel/types.h>

#define KERNEL_VIRTUAL_BASE 0x0ULL
#define KERNEL_PHYSICAL_BASE 0x100000ULL

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#define PTE_PRESENT    (1ULL << 0)
#define PTE_WRITABLE   (1ULL << 1)
#define PTE_USER       (1ULL << 2)
#define PTE_WRITE_THROUGH (1ULL << 3)
#define PTE_CACHE_DISABLE (1ULL << 4)
#define PTE_ACCESSED   (1ULL << 5)
#define PTE_DIRTY      (1ULL << 6)
#define PTE_HUGE       (1ULL << 7)
#define PTE_GLOBAL     (1ULL << 8)
#define PTE_NO_EXEC    (1ULL << 63)

static inline u64 phys_to_virt(u64 phys) {
    return phys;  // Identity mapping
}

static inline u64 virt_to_phys(u64 virt) {
    return virt;  // Identity mapping
}

static inline void* phys_to_virt_ptr(void* phys) {
    return phys;
}

static inline void* virt_to_phys_ptr(void* virt) {
    return virt;
}

void paging_init(u64 total_phys_mem);
u64 *paging_get_kernel_cr3(void);
void paging_switch(u64 *pml4);
u64 *paging_create_user_task(void *user_mem, sz user_size);
void paging_destroy_user_task(u64 *pml4);
int paging_map_page(u64 *pml4, u64 virt_addr, u64 phys_addr, u64 flags);
int paging_map_range(u64 *pml4, u64 virt_start, u64 phys_start, sz size, u64 flags);
void paging_unmap_page(u64 *pml4, u64 virt_addr);
void paging_unmap_range(u64 *pml4, u64 virt_start, sz size);

void paging_print_stats(void);

#endif
