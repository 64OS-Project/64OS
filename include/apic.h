#ifndef APIC_H
#define APIC_H

#include <kernel/types.h>
#include <apicregs.h>

/*
 * ============================================================================= Local APIC ============================================================================
 */

bool apic_init(u32 base_addr);
void apic_enable(void);
void apic_disable(void);
void apic_eoi(void);

u32 apic_read_reg(u32 reg);
void apic_write_reg(u32 reg, u32 val);

u32 apic_get_id(void);
u32 apic_get_version(void);

/*
 * IPI
 */
void apic_send_ipi(u32 apic_id, u32 vector);
void apic_send_broadcast(u32 vector);
void apic_send_init(u32 apic_id);
void apic_send_startup(u32 apic_id, u32 vector);

#endif