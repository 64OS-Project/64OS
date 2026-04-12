#ifndef DRIVER_H
#define DRIVER_H

#include <kernel/types.h>
#include <kernel/list.h>

#define DRIVER_CRITICAL_0   0  /*
 * Most critical (kernel cannot load without it)
 */
#define DRIVER_CRITICAL_1   1  /*
 * Very important
 */
#define DRIVER_CRITICAL_2   2  /*
 * Important
 */
#define DRIVER_CRITICAL_3   3  /*
 * Ordinary
 */
#define DRIVER_CRITICAL_4   4  /*
 * Not very important
 */
#define DRIVER_CRITICAL_5   5  /*
 * Not critical (can be skipped)
 */

typedef struct driver {
    list_head_t node;

    char name[32];
    char desc[64];
    u8 critical_level;

    bool initialized;
    
    int (*probe)(struct driver *drv);           /*
 * Availability check
 */
    int (*init)(struct driver *drv);            /*
 * Initialization
 */
    void (*remove)(struct driver *drv);         /*
 * Delete
 */

    void *priv;
} driver_t;

void driver_subsystem_init(void);

/*
 * Driver registration
 */
bool driver_register(driver_t *drv);

/*
 * Driver delete
 */
bool driver_unregister(driver_t *drv);

/*
 * List of drivers
 */
void driver_list(void);

/*
 * Get driver info
 */
void driver_get_info(driver_t *drv, char *buf, u32 bufsize);

/*
 * ============================================================================= Driver lookup functions ============================================================================
 */

/*
 * Search driver by name
 */
driver_t* driver_find_by_name(const char *name);

/*
 * Get driver count
 */
u32 driver_get_count(void);

/*
 * Get driver by index
 */
driver_t* driver_get_by_index(u32 index);

void* driver_get_private(driver_t *drv, u32 size);

/*
 * Automatic registration macros
 */
#define DRIVER_REGISTER(name) \
    static void __attribute__((constructor)) _reg_##name(void) { \
        extern driver_t name; \
        driver_register(&name); \
    }

/*
 * Macros for driver
 */
#define DRIVER_DEFINE(name, desc, crit, probe_fn, init_fn, remove_fn) \
    driver_t name = { \
        .name = #name, \
        .description = desc, \
        .critical_level = crit, \
        .probe = probe_fn, \
        .init = init_fn, \
        .remove = remove_fn, \
    }; \
    DRIVER_REGISTER(name)

#endif

    
