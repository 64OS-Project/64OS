#ifndef IOAPIC_H
#define IOAPIC_H

#include <kernel/types.h>
#include <kernel/list.h>

/*
 * ============================================================================= IOAPIC Driver API ============================================================================
 */

/*
 * Flags for interrupt configuration
 */
#define IOAPIC_FLAG_ACTIVE_LOW      (1 << 0)
#define IOAPIC_FLAG_LEVEL_TRIGGERED (1 << 1)
#define IOAPIC_FLAG_ACTIVE_HIGH     (0 << 0)
#define IOAPIC_FLAG_EDGE_TRIGGERED  (0 << 1)
#define IOAPIC_REDTBL_BASE       0x10

typedef struct {
    u32 gsi;
    u32 flags;
    bool valid;
} ioapic_override_t;

typedef struct ioapic_device {
    list_head_t node;
    u32 id;
    u32 address;
    u32 gsi_base;
    u32 version;
    u32 max_redir;
    volatile void *virt_addr;
    bool enabled;
} ioapic_device_t;

extern ioapic_override_t ioapic_overrides[256];

/*
 * ============================================================================= IOAPIC Management Functions ============================================================================
 */

/*
 * Get number of IOAPICs in system
 */
u32 ioapic_get_count(void);

/*
 * Get IOAPIC version by index
 */
u32 ioapic_get_version(u32 index);

void ioapic_get_redirection(ioapic_device_t *ioapic, u32 index,
                                    u32 *low, u32 *high);

/*
 * Get GSI base for IOAPIC by index
 */
u32 ioapic_get_gsi_base(u32 index);

/*
 * Get number of IRQs supported by IOAPIC
 */
u32 ioapic_get_irq_count(u32 index);

/*
 * ============================================================================= IRQ Routing Functions ============================================================================
 */

/*
 * Redirect a GSI to a specific vector on a specific APIC
 */
bool ioapic_redirect_irq(u32 gsi, u8 vector, u32 apic_id, u32 flags);

/*
 * Remove redirection for a GSI
 */
bool ioapic_unredirect_irq(u32 gsi);

/*
 * Mask/unmask specific IRQ
 */
void ioapic_mask_irq(u32 gsi);
void ioapic_unmask_irq(u32 gsi);

/*
 * Mask/unmask all IRQs on all IOAPICs
 */
void ioapic_mask_all(void);
void ioapic_unmask_all(void);

/*
 * Send EOI for level-triggered interrupt
 */
void ioapic_eoi(u32 gsi);

/*
 * Process interrupt source overrides from MADT
 */
bool ioapic_process_overrides(void);

bool ioapic_get_override(u32 source, u32 *gsi, u32 *flags);

void ioapic_read_redirection(void *ioapic, u32 index, u32 *low, u32 *high);
ioapic_device_t* ioapic_get_first(void);

#endif /*
 * IOAPIC_H
 */