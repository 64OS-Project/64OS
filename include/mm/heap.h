#ifndef KERNEL_MALLOC_H
#define KERNEL_MALLOC_H

#include <kernel/types.h>
#include <libk/string.h>

/*
 * structure of statistics
 */
typedef struct
{
    sz total_managed; /*
 * payloads + headers (in bytes)
 */
    sz used_payload;
    sz free_payload;
    sz largest_free;
    sz num_blocks;
    sz num_used;
    sz num_free;
} kmalloc_stats_t;

int malloc_init(void *heap_start, sz heap_size);
void *malloc(sz size);
void free(void *ptr);
void *realloc(void *ptr, sz new_size);
void get_kmalloc_stats(kmalloc_stats_t *st);
// Aligned selection
void* malloc_aligned(sz size, sz alignment);
void free_aligned(void* ptr);

#endif
