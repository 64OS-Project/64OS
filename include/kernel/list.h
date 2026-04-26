// include/kernel/list.h
#ifndef KERNEL_LIST_H
#define KERNEL_LIST_H

#include <kernel/types.h>

/*
 * Doubly linked list structure
 */
typedef struct list_head {
    struct list_head *next;
    struct list_head *prev;
} list_head_t;

/*
 * Initialize list head
 */
static inline void list_init(list_head_t *head) {
    head->next = head;
    head->prev = head;
}

/*
 * Add node at beginning
 */
static inline void list_add(list_head_t *head, list_head_t *node) {
    node->next = head->next;
    node->prev = head;
    head->next->prev = node;
    head->next = node;
}

/*
 * Add node at end
 */
static inline void list_add_tail(list_head_t *head, list_head_t *node) {
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

/*
 * Remove node from list
 */
static inline void list_del(list_head_t *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = NULL;
    node->prev = NULL;
}

/*
 * Check if list is empty
 */
static inline bool list_empty(list_head_t *head) {
    return head->next == head;
}

/*
 * Get entry from list node
 */
#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/*
 * Iterate over list
 */
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/*
 * Safe iteration (allows deletion)
 */
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

#endif /*
 * KERNEL_LIST_H
 */