#include <kernel/driver.h>
#include <libk/string.h>

static list_head_t g_driver_list;
static u32 g_driver_count = 0;

static u8 g_driver_private_pool[8192];  /*
 * 8KB pool for driver private data
 */
static u32 g_driver_private_offset = 0;

/*
 * ============================================================================= Public functions ============================================================================
 */

void driver_subsystem_init(void) {
    list_init(&g_driver_list);
    g_driver_count = 0;
}

bool driver_register(driver_t *drv) {
    if (!drv) return false;

    if (driver_find_by_name(drv->name)) {
        return false;
    }

    list_add_tail(&g_driver_list, &drv->node);
    g_driver_count++;
    
    drv->initialized = false;
    
    return true;
}

bool driver_unregister(driver_t *drv) {
    if (!drv) return false;

    if (drv->remove) {
        drv->remove(drv);
    }
   
    list_del(&drv->node);
    g_driver_count--;
    
    return true;
}

void driver_list(void) {
    list_head_t *pos;
    u32 i = 0;
    
    list_for_each(pos, &g_driver_list) {
        driver_t *drv = list_entry(pos, driver_t, node);
    }
}

void driver_get_info(driver_t *drv, char *buf, u32 bufsize) {
    if (!drv || !buf || bufsize == 0) return;
    
    snprintf(buf, bufsize, 
             "Driver: %s\n"
             "  Description: %s\n"
             "  Criticality: %d\n"
             "  Status: %s",
             drv->name,
             drv->desc,
             drv->critical_level,
             drv->initialized ? "Initialized" : "Ready");
}

driver_t* driver_find_by_name(const char *name) {
    list_head_t *pos;
    
    list_for_each(pos, &g_driver_list) {
        driver_t *drv = list_entry(pos, driver_t, node);
        if (strcmp(drv->name, name) == 0) {
            return drv;
        }
    }
    
    return NULL;
}

u32 driver_get_count(void) {
    return g_driver_count;
}

driver_t* driver_get_by_index(u32 index) {
    list_head_t *pos;
    u32 i = 0;
    
    list_for_each(pos, &g_driver_list) {
        if (i == index) {
            return list_entry(pos, driver_t, node);
        }
        i++;
    }
    
    return NULL;
}

void* driver_get_private(driver_t *drv, u32 size) {
    if (!drv) return NULL;
    
    /*
 * If already has private data, return it
 */
    if (drv->priv) {
        return drv->priv;
    }
    
    /*
 * Check if we have enough space
 */
    if (g_driver_private_offset + size > sizeof(g_driver_private_pool)) {
        return NULL;
    }
    
    /*
 * Allocate from pool
 */
    drv->priv = &g_driver_private_pool[g_driver_private_offset];
    g_driver_private_offset += size;
    
    /*
 * Zero it out
 */
    memset(drv->priv, 0, size);
    
    return drv->priv;
}
