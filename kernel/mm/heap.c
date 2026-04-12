#include <mm/heap.h>
#include <kernel/types.h>
#include <libk/string.h>
#include <kernel/panic.h>

/*
 * Configuration
 */
#define ALIGN 8
#define MAGIC 0xB16B00B5U
#define SIZE_MAX 18446744073709551615U

/*
 * Block header (payload comes immediately after the header)
 */
typedef struct block_header
{
    u32 magic;
    sz size; /*
 * payload size in bytes
 */
    int free;    /*
 * 1 if free, 0 if busy
 */
    struct block_header *prev;
    struct block_header *next;
} block_header_t;

#define MIN_SPLIT_SIZE (sizeof(block_header_t) + ALIGN)

/*
 * Global
 */
static block_header_t *heap_head = NULL;
static block_header_t *heap_tail = NULL;
static void *managed_heap_end = NULL;
static unsigned char *brk_ptr = NULL; /*
 * current limit (bump pointer inside area)
 */

/*
 * Symbos from link.ld
 */
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
 * Initialization: pass _heap_start and size (in bytes)
 */
int malloc_init(void *heap_start, sz heap_size)
{
    if (!heap_start || heap_size < sizeof(block_header_t))
        return -1;

    heap_head = (block_header_t *)heap_start;
    heap_head->magic = MAGIC;
    heap_head->size = heap_size - sizeof(block_header_t);
    heap_head->free = 1;
    heap_head->prev = heap_head->next = NULL;

    heap_tail = heap_head;
    managed_heap_end = (char *)heap_start + heap_size;
    brk_ptr = (unsigned char *)heap_start + heap_size; /*
 * brk_ptr stores the top of the reserved area
 */
    return 0;
}

static void *simple_morecore(sz bytes)
{
    sz req = align_up(bytes);

    unsigned char *new_brk = (unsigned char *)brk_ptr - req;
    if ((void *)new_brk < (void *)&_heap_start)
    {
        return NULL;
    }

    brk_ptr = new_brk;
    return (void *)brk_ptr;
}

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

static void split_block(block_header_t *h, sz req_size)
{
    if (!h)
        return;
    if (h->size < req_size + MIN_SPLIT_SIZE)
        return;

    char *new_hdr_addr = (char *)header_to_payload(h) + req_size;
    block_header_t *newh = (block_header_t *)new_hdr_addr;
    newh->magic = MAGIC;
    newh->free = 1;
    newh->size = h->size - req_size - sizeof(block_header_t);
    newh->prev = h;
    newh->next = h->next;
    if (newh->next)
        newh->next->prev = newh;
    h->next = newh;
    h->size = req_size;
    if (heap_tail == h)
        heap_tail = newh;
}

static void coalesce(block_header_t *h)
{
    if (!h)
        return;
    if (h->next && h->next->free)
    {
        block_header_t *n = h->next;
        h->size = h->size + sizeof(block_header_t) + n->size;
        h->next = n->next;
        if (n->next)
            n->next->prev = h;
        if (heap_tail == n)
            heap_tail = h;
    }
    if (h->prev && h->prev->free)
    {
        block_header_t *p = h->prev;
        p->size = p->size + sizeof(block_header_t) + h->size;
        p->next = h->next;
        if (h->next)
            h->next->prev = p;
        if (heap_tail == h)
            heap_tail = p;
    }
}

static int heap_expand(sz bytes)
{
    sz need = align_up(bytes + sizeof(block_header_t));
    void *p = simple_morecore(need);
    if (!p)
        return 0;

    block_header_t *h = (block_header_t *)p;
    h->magic = MAGIC;
    h->free = 1;
    h->size = need - sizeof(block_header_t);
    h->prev = heap_tail;
    h->next = NULL;
    if (heap_tail)
        heap_tail->next = h;
    heap_tail = h;
    if (!heap_head)
        heap_head = h;
    return 1;
}

void *malloc(sz size)
{
    if (size == 0)
        return NULL;
    size = align_up(size);

    block_header_t *fit = find_fit(size);
    int expansion_attempts = 0;
    const int MAX_EXPANSION_ATTEMPTS = 10;
    while (!fit)
    {
        if (++expansion_attempts > MAX_EXPANSION_ATTEMPTS)
        {
            panic("KERNEL HEAP EXHAUSTED");
        }

        if (!heap_expand(size))
        {
            panic("KERNEL HEAP EXPANSION FAILED");
        }
        fit = find_fit(size);
    }
    if (!fit)
        panic("MALLOC CRITICAL FAILURE");
    split_block(fit, size);
    fit->free = 0;
    return header_to_payload(fit);
}

void free(void *ptr)
{
    if (!ptr)
        return;

    block_header_t *h = payload_to_header(ptr);

    if (h->magic != MAGIC)
       panic("HEAP CORRUPTION INVALID MAGIC");

    if (h->free)
        panic("DOUBLE FREE");

    h->free = 1;

    coalesce(h);
}

void *realloc(void *ptr, sz new_size)
{
    if (!ptr)
        return malloc(new_size);
    if (new_size == 0)
    {
        free(ptr);
        return NULL;
    }

    block_header_t *h = payload_to_header(ptr);
    if (h->magic != MAGIC)
        return NULL;

    new_size = align_up(new_size);
    if (new_size <= h->size)
    {
        split_block(h, new_size);
        return ptr;
    }

    if (h->next && h->next->free)
    {
        sz sum = h->size;
        block_header_t *cur = h->next;
        while (cur && cur->free && sum < new_size)
        {
            sum += sizeof(block_header_t) + cur->size;
            cur = cur->next;
        }
        if (sum >= new_size)
        {
            block_header_t *to = h->next;
            while (to && to->free && h->size < new_size)
            {
                h->size = h->size + sizeof(block_header_t) + to->size;
                to = to->next;
            }
            h->next = to;
            if (to)
                to->prev = h;
            split_block(h, new_size);
            h->free = 0;
            return ptr;
        }
    }

    void *newp = malloc(new_size);
    if (!newp)
        return NULL;
    sz copy = (h->size < new_size) ? h->size : new_size;
    memcpy(newp, ptr, copy);
    free(ptr);
    return newp;
}

void *calloc(sz nmemb, sz size)
{
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        return NULL;
    }

    sz total_size = nmemb * size;

    if (total_size == 0) {
        return NULL;
    }

    void *ptr = malloc(total_size);
    if (!ptr) {
        return NULL;
    }

    memset(ptr, 0, total_size);

    return ptr;
}

void get_kmalloc_stats(kmalloc_stats_t *st)
{
    if (!st)
        return;
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

static sz kstrlen(const char *s)
{
    sz i = 0;
    if (!s)
        return 0;
    while (s[i])
        ++i;
    return i;
}

static char *u32_to_dec(u32 v, char *buf)
{
    char tmp[32];
    int i = 0;
    if (v == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }
    while (v)
    {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }
    for (int j = 0; j < i; ++j)
        buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
    return buf;
}

void* malloc_aligned(sz size, sz alignment) {
    if (alignment < ALIGN) {
        alignment = ALIGN;
    }

    if ((alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    sz total_size = size + alignment - 1 + sizeof(void*);
    void* original_ptr = malloc(total_size);
    if (!original_ptr) {
        return NULL;
    }

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

