#include <kernel/paging.h>
#include <kernel/types.h>
#include <mm/pmm.h>
#include <libk/string.h>
#include <kernel/panic.h>

#define PML4_SHIFT 39
#define PDPT_SHIFT 30
#define PD_SHIFT   21
#define PT_SHIFT   12

#define PML4_INDEX(va) (((u64)(va) >> PML4_SHIFT) & 0x1FF)
#define PDPT_INDEX(va) (((u64)(va) >> PDPT_SHIFT) & 0x1FF)
#define PD_INDEX(va)   (((u64)(va) >> PD_SHIFT) & 0x1FF)
#define PT_INDEX(va)   (((u64)(va) >> PT_SHIFT) & 0x1FF)

#define PAGE_MASK      0xFFFFFFFFF000ULL
#define HUGE_PAGE_MASK 0xFFFFFFFFFFE00000ULL

static u64 *kernel_pml4 = NULL;
static u64 total_phys_pages = 0;
static bool paging_initialized = false;

extern pmm_t pmm;

static u64* get_next_level(u64 *table, u64 index, u64 flags, bool allocate) {
    u64 entry = table[index];
    
    if (!(entry & PTE_PRESENT)) {
        if (!allocate) return NULL;

        u64 *new_table = (u64*)pmm_alloc_page(&pmm);
        if (!new_table) {
            panic("PAGING: Failed to allocate page table");
        }

        memset(new_table, 0, PAGE_SIZE);

        u64 phys_addr = (u64)virt_to_phys_ptr(new_table);
        table[index] = phys_addr | flags | PTE_PRESENT | PTE_WRITABLE;
        
        return new_table;
    }

    u64 phys_addr = entry & PAGE_MASK;
    return (u64*)phys_to_virt_ptr((void*)phys_addr);
}

void paging_init(u64 total_phys_mem) {
    if (paging_initialized) return;
    
    total_phys_pages = total_phys_mem / PAGE_SIZE;

    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    kernel_pml4 = (u64*)phys_to_virt_ptr((void*)(cr3 & PAGE_MASK));
    
    paging_initialized = true;
}

u64 *paging_get_kernel_cr3(void) {
    return kernel_pml4;
}

void paging_switch(u64 *pml4) {
    if (!pml4) return;
    
    u64 phys_addr = (u64)virt_to_phys_ptr(pml4);
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys_addr) : "memory");
}

int paging_map_page(u64 *pml4, u64 virt_addr, u64 phys_addr, u64 flags) {
    if (!pml4) return -1;

    flags |= PTE_PRESENT | PTE_WRITABLE;

    u64 *pdpt = get_next_level(pml4, PML4_INDEX(virt_addr), flags, true);
    if (!pdpt) return -1;
    
    u64 *pdir = get_next_level(pdpt, PDPT_INDEX(virt_addr), flags, true);
    if (!pdir) return -1;
    
    u64 *ptbl = get_next_level(pdir, PD_INDEX(virt_addr), flags, true);
    if (!ptbl) return -1;
 
    u64 pte_index = PT_INDEX(virt_addr);
    ptbl[pte_index] = (phys_addr & PAGE_MASK) | flags;

    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
    
    return 0;
}

int paging_map_range(u64 *pml4, u64 virt_start, u64 phys_start, sz size, u64 flags) {
    if (!pml4 || size == 0) return -1;
    
    u64 virt = virt_start & ~(PAGE_SIZE - 1);
    u64 phys = phys_start & ~(PAGE_SIZE - 1);
    u64 end = virt_start + size;
    
    while (virt < end) {
        if (paging_map_page(pml4, virt, phys, flags) != 0) {
            return -1;
        }
        virt += PAGE_SIZE;
        phys += PAGE_SIZE;
    }
    
    return 0;
}

void paging_unmap_page(u64 *pml4, u64 virt_addr) {
    if (!pml4) return;

    u64 *pdpt = get_next_level(pml4, PML4_INDEX(virt_addr), 0, false);
    if (!pdpt) return;
    
    u64 *pdir = get_next_level(pdpt, PDPT_INDEX(virt_addr), 0, false);
    if (!pdir) return;
    
    u64 *ptbl = get_next_level(pdir, PD_INDEX(virt_addr), 0, false);
    if (!ptbl) return;

    u64 pte_index = PT_INDEX(virt_addr);
    ptbl[pte_index] = 0;

    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

void paging_unmap_range(u64 *pml4, u64 virt_start, sz size) {
    if (!pml4 || size == 0) return;
    
    u64 virt = virt_start & ~(PAGE_SIZE - 1);
    u64 end = virt_start + size;
    
    while (virt < end) {
        paging_unmap_page(pml4, virt);
        virt += PAGE_SIZE;
    }
}


u64 *paging_create_user_task(void *user_mem, sz user_size) {
    if (!kernel_pml4) return NULL;
    
    u64 *user_pml4 = (u64*)pmm_alloc_page(&pmm);
    if (!user_pml4) return NULL;
    
    user_pml4 = (u64*)phys_to_virt_ptr(user_pml4);
    memset(user_pml4, 0, PAGE_SIZE);

    for (int i = 256; i < 512; i++) {
        u64 kernel_entry = kernel_pml4[i];
        if (kernel_entry & PTE_PRESENT) {
            user_pml4[i] = kernel_entry;
        }
    }

    if (user_mem && user_size > 0) {
        u64 user_start = (u64)user_mem;
        u64 user_end = user_start + user_size;

        u64 flags = PTE_USER | PTE_WRITABLE;
        
        if (paging_map_range(user_pml4, user_start, user_start, user_size, flags) != 0) {
            paging_destroy_user_task(user_pml4);
            return NULL;
        }
    }
    
    return user_pml4;
}

void paging_destroy_user_task(u64 *pml4) {
    if (!pml4) return;

    for (int i = 0; i < 256; i++) {
        if (pml4[i] & PTE_PRESENT) {
            u64 *pdpt = (u64*)phys_to_virt_ptr((void*)(pml4[i] & PAGE_MASK));
            
            for (int j = 0; j < 512; j++) {
                if (pdpt[j] & PTE_PRESENT) {
                    u64 *pdir = (u64*)phys_to_virt_ptr((void*)(pdpt[j] & PAGE_MASK));
                    
                    for (int k = 0; k < 512; k++) {
                        if (pdir[k] & PTE_PRESENT && !(pdir[k] & PTE_HUGE)) {
                            u64 *ptbl = (u64*)phys_to_virt_ptr((void*)(pdir[k] & PAGE_MASK));
                            
                            for (int l = 0; l < 512; l++) {
                                if (ptbl[l] & PTE_PRESENT) {
                                    u64 phys = ptbl[l] & PAGE_MASK;
                                    pmm_free_page(&pmm, (void*)phys);
                                }
                            }
                            pmm_free_page(&pmm, (void*)virt_to_phys_ptr(ptbl));
                        } else if (pdir[k] & PTE_PRESENT && (pdir[k] & PTE_HUGE)) {
                            u64 phys = pdir[k] & HUGE_PAGE_MASK;
                            pmm_free_page(&pmm, (void*)phys);
                        }
                    }
                    pmm_free_page(&pmm, (void*)virt_to_phys_ptr(pdir));
                }
            }
            pmm_free_page(&pmm, (void*)virt_to_phys_ptr(pdpt));
        }
    }

    pmm_free_page(&pmm, (void*)virt_to_phys_ptr(pml4));
}

void paging_print_stats(void) {
    if (!kernel_pml4) {
        return;
    }
    
    u64 total_entries = 0;
    u64 present_entries = 0;
    
    for (int i = 0; i < 512; i++) {
        if (kernel_pml4[i] & PTE_PRESENT) {
            present_entries++;
        }
        total_entries++;
    }
}

int paging_map_user_region(u64 *pml4, void *addr, sz size) {
    u64 virt = (u64)addr;
    u64 flags = PTE_USER | PTE_WRITABLE;
    
    return paging_map_range(pml4, virt, virt, size, flags);
}

void paging_unmap_user_region(u64 *pml4, void *addr, sz size) {
    paging_unmap_range(pml4, (u64)addr, size);
}
