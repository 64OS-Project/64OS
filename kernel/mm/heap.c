#include <mm/heap.h>
#include <mm/pmm.h>
#include <kernel/types.h>
#include <libk/string.h>
#include <kernel/panic.h>
#include <kernel/paging.h>

/*
 * Configuration
 */
#define ALIGN 8
#define MAGIC 0xB16B00B5U
#define SIZE_MAX 18446744073709551615U
#define HEAP_MAX_SIZE (512 * 1024 * 1024)  // 512 MB максимум

/*
 * Block header
 */
typedef struct block_header
{
    u32 magic;
    sz size;
    int free;
    struct block_header *prev;
    struct block_header *next;
} block_header_t;

#define MIN_SPLIT_SIZE (sizeof(block_header_t) + ALIGN)

/*
 * Global
 */
static block_header_t *heap_head = NULL;
static block_header_t *heap_tail = NULL;
static void *heap_start_virt = NULL;
static void *heap_end_virt = NULL;
static sz heap_current_size = 0;

extern pmm_t pmm;
extern char _heap_start;
extern char _heap_end;

static inline sz align_up(sz n)
{
    return (n + (ALIGN - 1)) & ~(ALIGN - 1);
}

static inline void *header_to_payload(block_header_t *h)
{
    return (void *)((char *)h + sizeof(block_header_t));
}

static inline block_header_t *payload_to_header(void *p)
{
    return (block_header_t *)((char *)p - sizeof(block_header_t));
}

/*
 * Expand heap using PMM
 */
static int heap_expand(sz bytes)
{
    sz need = align_up(bytes + sizeof(block_header_t));
    sz pages_needed = (need + PAGE_SIZE - 1) / PAGE_SIZE;
    sz expand_size = pages_needed * PAGE_SIZE;
    
    if (heap_current_size + expand_size > HEAP_MAX_SIZE) {
        return 0;
    }
    
    // Allocate physical pages
    void *phys = pmm_alloc_range(&pmm, pages_needed);
    if (!phys) {
        return 0;
    }
    
    // Identity mapping (virtual = physical)
    void *virt = (void*)heap_end_virt;
    
    // Map pages
    for (sz i = 0; i < pages_needed; i++) {
        u64 phys_addr = (u64)phys + i * PAGE_SIZE;
        u64 virt_addr = (u64)virt + i * PAGE_SIZE;
        
        // Map page
        paging_map_page(paging_get_kernel_cr3(), virt_addr, phys_addr, 
                        PTE_PRESENT | PTE_WRITABLE);
    }
    
    // Create header for new block
    block_header_t *h = (block_header_t *)virt;
    h->magic = MAGIC;
    h->free = 1;
    h->size = expand_size - sizeof(block_header_t);
    h->prev = heap_tail;
    h->next = NULL;
    
    if (heap_tail) {
        heap_tail->next = h;
    } else {
        heap_head = h;
    }
    heap_tail = h;
    
    heap_end_virt = (void*)((uptr)heap_end_virt + expand_size);
    heap_current_size += expand_size;
    
    return 1;
}

/*
 * Initialize heap
 */
int malloc_init(void *heap_start, sz heap_size)
{
    if (!heap_start || heap_size < sizeof(block_header_t))
        return -1;
    
    heap_start_virt = heap_start;
    heap_end_virt = (void*)((uptr)heap_start + heap_size);
    heap_current_size = heap_size;
    
    heap_head = (block_header_t *)heap_start;
    heap_head->magic = MAGIC;
    heap_head->size = heap_size - sizeof(block_header_t);
    heap_head->free = 1;
    heap_head->prev = heap_head->next = NULL;
    
    heap_tail = heap_head;
    
    return 0;
}

/*
 * Find free block
 */
static block_header_t *find_fit(sz size)
{
    block_header_t *cur = heap_head;
    while (cur)
    {
        if (cur->free && cur->size >= size)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

/*
 * Split block
 */
static void split_block(block_header_t *h, sz req_size)
{
    if (!h) return;
    if (h->size < req_size + MIN_SPLIT_SIZE) return;
    
    char *new_hdr_addr = (char *)header_to_payload(h) + req_size;
    block_header_t *newh = (block_header_t *)new_hdr_addr;
    newh->magic = MAGIC;
    newh->free = 1;
    newh->size = h->size - req_size - sizeof(block_header_t);
    newh->prev = h;
    newh->next = h->next;
    if (newh->next) newh->next->prev = newh;
    h->next = newh;
    h->size = req_size;
    if (heap_tail == h) heap_tail = newh;
}

/*
 * Coalesce adjacent free blocks
 */
static void coalesce(block_header_t *h)
{
    if (!h) return;
    
    if (h->next && h->next->free)
    {
        block_header_t *n = h->next;
        h->size = h->size + sizeof(block_header_t) + n->size;
        h->next = n->next;
        if (n->next) n->next->prev = h;
        if (heap_tail == n) heap_tail = h;
    }
    
    if (h->prev && h->prev->free)
    {
        block_header_t *p = h->prev;
        p->size = p->size + sizeof(block_header_t) + h->size;
        p->next = h->next;
        if (h->next) h->next->prev = p;
        if (heap_tail == h) heap_tail = p;
    }
}

/*
 * malloc
 */
void *malloc(sz size)
{
    if (size == 0) return NULL;
    size = align_up(size);
    
    block_header_t *fit = find_fit(size);
    int attempts = 0;
    
    while (!fit)
    {
        if (++attempts > 10) {
            panic("KERNEL HEAP EXHAUSTED");
        }
        
        if (!heap_expand(size)) {
            panic("KERNEL HEAP EXPANSION FAILED");
        }
        fit = find_fit(size);
    }
    
    split_block(fit, size);
    fit->free = 0;
    return header_to_payload(fit);
}

/*
 * free
 */
void free(void *ptr)
{
    if (!ptr) return;
    
    block_header_t *h = payload_to_header(ptr);
    
    if (h->magic != MAGIC)
        panic("HEAP CORRUPTION INVALID MAGIC");
    
    if (h->free)
        panic("DOUBLE FREE");
    
    h->free = 1;
    coalesce(h);
}

/*
 * realloc
 */
void *realloc(void *ptr, sz new_size)
{
    if (!ptr) return malloc(new_size);
    if (new_size == 0)
    {
        free(ptr);
        return NULL;
    }
    
    block_header_t *h = payload_to_header(ptr);
    if (h->magic != MAGIC) return NULL;
    
    new_size = align_up(new_size);
    if (new_size <= h->size)
    {
        split_block(h, new_size);
        return ptr;
    }
    
    void *newp = malloc(new_size);
    if (!newp) return NULL;
    
    memcpy(newp, ptr, h->size);
    free(ptr);
    return newp;
}

/*
 * calloc
 */
void *calloc(sz nmemb, sz size)
{
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        return NULL;
    }
    
    sz total_size = nmemb * size;
    if (total_size == 0) return NULL;
    
    void *ptr = malloc(total_size);
    if (!ptr) return NULL;
    
    memset(ptr, 0, total_size);
    return ptr;
}

/*
 * Statistics
 */
void get_kmalloc_stats(kmalloc_stats_t *st)
{
    if (!st) return;
    
    st->total_managed = 0;
    st->used_payload = 0;
    st->free_payload = 0;
    st->largest_free = 0;
    st->num_blocks = st->num_used = st->num_free = 0;
    
    block_header_t *cur = heap_head;
    while (cur)
    {
        st->num_blocks++;
        st->total_managed += sizeof(block_header_t) + cur->size;
        if (cur->free)
        {
            st->num_free++;
            st->free_payload += cur->size;
            if (cur->size > st->largest_free)
                st->largest_free = cur->size;
        }
        else
        {
            st->num_used++;
            st->used_payload += cur->size;
        }
        cur = cur->next;
    }
}

/*
 * Aligned allocation
 */
void* malloc_aligned(sz size, sz alignment) {
    if (alignment < ALIGN) alignment = ALIGN;
    if ((alignment & (alignment - 1)) != 0) return NULL;
    
    sz total_size = size + alignment - 1 + sizeof(void*);
    void* original_ptr = malloc(total_size);
    if (!original_ptr) return NULL;
    
    uptr addr = (uptr)original_ptr + sizeof(void*);
    uptr aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
    
    void** ptr_store = (void**)(aligned_addr - sizeof(void*));
    *ptr_store = original_ptr;
    
    return (void*)aligned_addr;
}

void free_aligned(void* ptr) {
    if (!ptr) return;
    
    void** ptr_store = (void**)((uptr)ptr - sizeof(void*));
    void* original_ptr = *ptr_store;
    
    free(original_ptr);
}