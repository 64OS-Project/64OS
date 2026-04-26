#include <apic.h>
#include <ioapic.h>
#include <asm/mmio.h>
#include <asm/io.h>
#include <kernel/driver.h>
#include <acpi.h>
#include <libk/string.h>

ioapic_override_t ioapic_overrides[256];

/*
 * ============================================================================= IOAPIC Driver Structure ============================================================================
 */

/*
 * Global IOAPIC list
 */
static list_head_t g_ioapic_list;
static u32 g_ioapic_count = 0;

/*
 * IRQ redirection table (GSI -> vector mapping)
 */
#define MAX_GSI 256
static struct {
    u8 vector;
    bool masked;
    bool level_triggered;
    bool active_low;
    u32 dest_apic_id;
} g_irq_redirects[MAX_GSI];

/*
 * ============================================================================= IOAPIC Register Access ============================================================================
 */

#define IOAPIC_REG_ID        0x00
#define IOAPIC_REG_VERSION   0x01
#define IOAPIC_REG_ARB       0x02
#define IOAPIC_REDTBL_BASE   0x10

static inline u32 ioapic_read_reg(volatile void *base, u32 reg) {
    mmio_write32((volatile u32*)((uptr)base + 0x00), reg);   // IOREGSEL
    return mmio_read32((volatile u32*)((uptr)base + 0x10));  // IOWIN
}

static inline void ioapic_write_reg(volatile void *base, u32 reg, u32 val) {
    mmio_write32((volatile u32*)((uptr)base + 0x00), reg);   // IOREGSEL
    mmio_write32((volatile u32*)((uptr)base + 0x10), val);   // IOWIN
}

static void ioapic_set_redirection(ioapic_device_t *ioapic, u32 index, 
                                    u32 low, u32 high) {
    u32 reg_low = IOAPIC_REDTBL_BASE + index * 2;
    u32 reg_high = reg_low + 1;
    
    //Changing the order:
    ioapic_write_reg(ioapic->virt_addr, reg_high, high); // HIGH first
    ioapic_write_reg(ioapic->virt_addr, reg_low, low);   // LOW second
}

void ioapic_get_redirection(ioapic_device_t *ioapic, u32 index,
                                    u32 *low, u32 *high) {
    u32 reg_low = IOAPIC_REDTBL_BASE + index * 2;
    u32 reg_high = reg_low + 1;
    
    *low = ioapic_read_reg(ioapic->virt_addr, reg_low);
    *high = ioapic_read_reg(ioapic->virt_addr, reg_high);
}

/*
 * ============================================================================= IRQ Routing ============================================================================
 */

static ioapic_device_t *find_ioapic_for_gsi(u32 gsi) {
    list_head_t *pos;
    ioapic_device_t *best = NULL;
    
    list_for_each(pos, &g_ioapic_list) {
        ioapic_device_t *ioapic = list_entry(pos, ioapic_device_t, node);
        if (gsi >= ioapic->gsi_base && 
            gsi < ioapic->gsi_base + ioapic->max_redir) {
            return ioapic;
        }
        /*
 * Find the one with largest base <= gsi
 */
        if (ioapic->gsi_base <= gsi) {
            if (!best || ioapic->gsi_base > best->gsi_base) {
                best = ioapic;
            }
        }
    }
    
    return best;
}

/*
 * ============================================================================= Driver Interface ============================================================================
 */

static int ioapic_probe(driver_t *drv) {
    (void)drv;
    acpi_t *acpi = acpi_get_table();
    
    /*
 * Check if we have MADT with IOAPIC entries
 */
    if (!acpi || !acpi->madt) {
        return 1; /*
 * No ACPI or no MADT
 */
    }
    
    /*
 * We'll probe during init, always claim driver exists
 */
    return 0;
}

static int ioapic_init_driver(driver_t *drv) {
    (void)drv;
    
    /*
 * Initialize global list
 */
    list_init(&g_ioapic_list);
    g_ioapic_count = 0;
    
    /*
 * Initialize redirect table
 */
    memset(g_irq_redirects, 0, sizeof(g_irq_redirects));
    for (u32 i = 0; i < MAX_GSI; i++) {
        g_irq_redirects[i].masked = true;
        g_irq_redirects[i].vector = 0;
    }
    
    /*
 * Parse MADT for IOAPIC entries
 */
    acpi_t *acpi = acpi_get_table();
    if (!acpi || !acpi->madt) {
        return -1;
    }
    
    u8 *entry = (u8*)acpi->madt + sizeof(madt_t);
    u8 *end = (u8*)acpi->madt + acpi->madt->header.length;
    u32 ioapic_idx = 0;
    
    while (entry < end) {
        madt_entry_header_t *header = (madt_entry_header_t*)entry;
        
        if (header->type == MADT_TYPE_IO_APIC) {
            madt_io_apic_t *ioapic_entry = (madt_io_apic_t*)entry;
            
            ioapic_device_t *ioapic = (ioapic_device_t*)driver_get_private(drv, 
                sizeof(ioapic_device_t));
            if (!ioapic) {
                return -1;
            }
            
            ioapic->id = ioapic_entry->io_apic_id;
            ioapic->address = ioapic_entry->io_apic_address;
            ioapic->gsi_base = ioapic_entry->global_system_interrupt_base;
            ioapic->virt_addr = (volatile void*)(uptr)ioapic->address;
            ioapic->enabled = false;
            
            /*
 * Read version to determine max redirection entries
 */
            u32 version = ioapic_read_reg(ioapic->virt_addr, IOAPIC_REG_VERSION);
            ioapic->version = version & 0xFF;
            ioapic->max_redir = ((version >> 16) & 0xFF) + 1;
            
            /*
 * Add to global list
 */
            list_add_tail(&g_ioapic_list, &ioapic->node);
            g_ioapic_count++;
            ioapic_idx++;
        }
        
        entry += header->length;
    }
    
    if (g_ioapic_count == 0) {
        return -1; /*
 * No IOAPIC found
 */
    }
    
    /*
 * Mask all interrupts initially
 */
    ioapic_mask_all();
    
    return 0;
}

static void ioapic_remove(driver_t *drv) {
    (void)drv;
    
    /*
 * Mask all interrupts
 */
    ioapic_mask_all();
    
    /*
 * Clear list
 */
    list_head_t *pos, *n;
    list_for_each_safe(pos, n, &g_ioapic_list) {
        ioapic_device_t *ioapic = list_entry(pos, ioapic_device_t, node);
        list_del(&ioapic->node);
        g_ioapic_count--;
    }
}

driver_t g_ioapic_driver = {
    .name = "ioapic",
    .desc = "I/O Advanced Programmable Interrupt Controller Driver",
    .critical_level = DRIVER_CRITICAL_0,
    .probe = ioapic_probe,
    .init = ioapic_init_driver,
    .remove = ioapic_remove,
    .priv = NULL,
};

/*
 * ============================================================================= Public API Functions ============================================================================
 */

u32 ioapic_get_count(void) {
    return g_ioapic_count;
}

bool ioapic_redirect_irq(u32 gsi, u8 vector, u32 apic_id, u32 flags) {
    ioapic_device_t *ioapic = find_ioapic_for_gsi(gsi);
    if (!ioapic) {
        return false;
    }
    
    u32 index = gsi - ioapic->gsi_base;
    if (index >= ioapic->max_redir) {
        return false;
    }
    
    /*
 * Build redirection entry
 */
    u32 low = vector & 0xFF;
    low |= DELIVERY_FIXED;  /*
 * Fixed delivery mode
 */
    
    /*
 * Destination mode: physical
 */
    /*
 * No destination mode bit = physical
 */
    
    /*
 * Set polarity
 */
    if (flags & IOAPIC_FLAG_ACTIVE_LOW) {
        low |= IOAPIC_REDIR_POLARITY;
    }
    
    /*
 * Set trigger mode
 */
    if (flags & IOAPIC_FLAG_LEVEL_TRIGGERED) {
        low |= IOAPIC_REDIR_TRIGGER;
    }
    
    /*
 * Masked? (start masked, unmask later)
 */
    low |= IOAPIC_REDIR_MASKED;
    
    u32 high = apic_id << 24;
    
    /*
 * Save redirect info
 */
    g_irq_redirects[gsi].vector = vector;
    g_irq_redirects[gsi].level_triggered = !!(flags & IOAPIC_FLAG_LEVEL_TRIGGERED);
    g_irq_redirects[gsi].active_low = !!(flags & IOAPIC_FLAG_ACTIVE_LOW);
    g_irq_redirects[gsi].dest_apic_id = apic_id;
    g_irq_redirects[gsi].masked = true;
    
    /*
 * Program IOAPIC
 */
    ioapic_set_redirection(ioapic, index, low, high);
    
    return true;
}

bool ioapic_unredirect_irq(u32 gsi) {
    ioapic_device_t *ioapic = find_ioapic_for_gsi(gsi);
    if (!ioapic) {
        return false;
    }
    
    u32 index = gsi - ioapic->gsi_base;
    if (index >= ioapic->max_redir) {
        return false;
    }
    
    /*
 * Mask the interrupt
 */
    u32 low, high;
    ioapic_get_redirection(ioapic, index, &low, &high);
    low |= IOAPIC_REDIR_MASKED;
    ioapic_set_redirection(ioapic, index, low, high);
    
    /*
 * Clear redirect info
 */
    g_irq_redirects[gsi].vector = 0;
    g_irq_redirects[gsi].masked = true;
    
    return true;
}

void ioapic_mask_irq(u32 gsi) {
    ioapic_device_t *ioapic = find_ioapic_for_gsi(gsi);
    if (!ioapic) return;
    
    u32 index = gsi - ioapic->gsi_base;
    if (index >= ioapic->max_redir) return;
    
    u32 low, high;
    ioapic_get_redirection(ioapic, index, &low, &high);
    low |= IOAPIC_REDIR_MASKED;  // Setting the mask bit
    ioapic_set_redirection(ioapic, index, low, high);
    
    g_irq_redirects[gsi].masked = true;
}

void ioapic_unmask_irq(u32 gsi) {
    ioapic_device_t *ioapic = find_ioapic_for_gsi(gsi);
    if (!ioapic) return;
    
    u32 index = gsi - ioapic->gsi_base;
    if (index >= ioapic->max_redir) return;
    
    u32 low, high;
    ioapic_get_redirection(ioapic, index, &low, &high);
    low &= ~IOAPIC_REDIR_MASKED;  // Removing the bit mask
    ioapic_set_redirection(ioapic, index, low, high);
    
    g_irq_redirects[gsi].masked = false;
}

void ioapic_mask_all(void) {
    list_head_t *pos;
    
    list_for_each(pos, &g_ioapic_list) {
        ioapic_device_t *ioapic = list_entry(pos, ioapic_device_t, node);
        
        for (u32 i = 0; i < ioapic->max_redir; i++) {
            u32 low, high;
            ioapic_get_redirection(ioapic, i, &low, &high);
            low |= IOAPIC_REDIR_MASKED;
            ioapic_set_redirection(ioapic, i, low, high);
            
            u32 gsi = ioapic->gsi_base + i;
            if (gsi < MAX_GSI) {
                g_irq_redirects[gsi].masked = true;
            }
        }
    }
}

void ioapic_unmask_all(void) {
    list_head_t *pos;
    
    list_for_each(pos, &g_ioapic_list) {
        ioapic_device_t *ioapic = list_entry(pos, ioapic_device_t, node);
        
        for (u32 i = 0; i < ioapic->max_redir; i++) {
            u32 low, high;
            ioapic_get_redirection(ioapic, i, &low, &high);
            low &= ~IOAPIC_REDIR_MASKED;
            ioapic_set_redirection(ioapic, i, low, high);
            
            u32 gsi = ioapic->gsi_base + i;
            if (gsi < MAX_GSI) {
                g_irq_redirects[gsi].masked = false;
            }
        }
    }
}

void ioapic_eoi(u32 gsi) {
    (void)gsi;  // gsi is not used, EOI is always via LAPIC
    apic_eoi();  // We ALWAYS send EOI to LAPIC
}

u32 ioapic_get_version(u32 index) {
    list_head_t *pos;
    u32 current = 0;
    
    list_for_each(pos, &g_ioapic_list) {
        if (current == index) {
            ioapic_device_t *ioapic = list_entry(pos, ioapic_device_t, node);
            return ioapic->version;
        }
        current++;
    }
    
    return 0;
}

u32 ioapic_get_gsi_base(u32 index) {
    list_head_t *pos;
    u32 current = 0;
    
    list_for_each(pos, &g_ioapic_list) {
        if (current == index) {
            ioapic_device_t *ioapic = list_entry(pos, ioapic_device_t, node);
            return ioapic->gsi_base;
        }
        current++;
    }
    
    return 0;
}

u32 ioapic_get_irq_count(u32 index) {
    list_head_t *pos;
    u32 current = 0;
    
    list_for_each(pos, &g_ioapic_list) {
        if (current == index) {
            ioapic_device_t *ioapic = list_entry(pos, ioapic_device_t, node);
            return ioapic->max_redir;
        }
        current++;
    }
    
    return 0;
}

/*
 * ============================================================================= Interrupt Source Override Processing ============================================================================
 */

bool ioapic_process_overrides(void) {
    acpi_t *acpi = acpi_get_table();
    if (!acpi || !acpi->madt) {
        return false;
    }

    memset(ioapic_overrides, 0, sizeof(ioapic_overrides));
    
    u8 *entry = (u8*)acpi->madt + sizeof(madt_t);
    u8 *end = (u8*)acpi->madt + acpi->madt->header.length;
    
    while (entry < end) {
        madt_entry_header_t *header = (madt_entry_header_t*)entry;
        
        if (header->type == MADT_TYPE_INT_SOURCE_OVERRIDE) {
            madt_int_source_override_t *override = (madt_int_source_override_t*)entry;
            
            /*
 * Build flags
 */
            u32 flags = 0;
            
            /*
 * Polarity: 0 = conforms, 1 = active high, 3 = active low
 */
            if ((override->flags & 0x3) == 0x3) { /*
 * Active low
 */
                flags |= IOAPIC_FLAG_ACTIVE_LOW;
            }
            
            /*
 * Trigger: 0 = conforms, 1 = edge, 3 = level
 */
            if (((override->flags >> 2) & 0x3) == 0x3) { /*
 * Level triggered
 */
                flags |= IOAPIC_FLAG_LEVEL_TRIGGERED;
            }
            
            /*
 * Save override mapping (source IRQ -> GSI)
 */
            g_irq_redirects[override->source].vector = 0; /*
 * Will be set later
 */
            g_irq_redirects[override->source].level_triggered = 
                !!(flags & IOAPIC_FLAG_LEVEL_TRIGGERED);
            g_irq_redirects[override->source].active_low = 
                !!(flags & IOAPIC_FLAG_ACTIVE_LOW);

	    ioapic_overrides[override->source].gsi = override->global_system_interrupt;
    	    ioapic_overrides[override->source].flags = flags;
    	    ioapic_overrides[override->source].valid = true;
        }
        
        entry += header->length;
    }
    
    return true;
}

bool ioapic_get_override(u32 source, u32 *gsi, u32 *flags) {
    if (source >= 256) return false;
    
    if (ioapic_overrides[source].valid) {
        if (gsi) *gsi = ioapic_overrides[source].gsi;
        if (flags) *flags = ioapic_overrides[source].flags;
        return true;
    }
    
    /*
 * No override - use source as GSI and standard flags
 */
    if (gsi) *gsi = source;
    if (flags) *flags = IOAPIC_FLAG_EDGE_TRIGGERED | IOAPIC_FLAG_ACTIVE_HIGH;
    return true;
}

// Helper functions for debugging
void ioapic_read_redirection(void *ioapic_ptr, u32 index, u32 *low, u32 *high) {
    ioapic_device_t *ioapic = (ioapic_device_t*)ioapic_ptr;
    if (!ioapic || !ioapic->virt_addr) return;
    
    u32 reg_low = IOAPIC_REDTBL_BASE + index * 2;
    u32 reg_high = reg_low + 1;
    
    *low = ioapic_read_reg(ioapic->virt_addr, reg_low);
    *high = ioapic_read_reg(ioapic->virt_addr, reg_high);
}

ioapic_device_t* ioapic_get_first(void) {
    if (list_empty(&g_ioapic_list)) {
        return NULL;
    }
    return list_entry(g_ioapic_list.next, ioapic_device_t, node);
}
