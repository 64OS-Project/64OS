#ifndef ACPI_H
#define ACPI_H

#include <acpitables.h>
#include <kernel/types.h>

/*
 * Processor info
 */
typedef struct {
    u8 acpi_id;
    u8 apic_id;
    bool enabled;
} acpi_processor_t;

/*
 * IO APIC info
 */
typedef struct {
    u32 address;
    u32 gsi_base;
} acpi_io_apic_t;

/*
 * Interrupt override info
 */
typedef struct {
    u8  bus;
    u8  source;
    u32 gsi;
    u16 flags;
} acpi_int_override_t;

/*
 * APIC info
 */
typedef struct {
    u32 local_apic_address;
    u32 processor_count;
    acpi_processor_t processors[64];
    u32 io_apic_count;
    acpi_io_apic_t io_apics[16];
    u32 int_override_count;
    acpi_int_override_t int_overrides[16];
} apic_info_t;

/*
 * Main ACPI structure
 */
typedef struct {
    rsdp_v2_t *rsdp;
    rsdt_t    *rsdt;
    xsdt_t    *xsdt;
    fadt_t    *fadt;
    madt_t    *madt;
    hpet_t    *hpet;
    mcfg_t    *mcfg;
    
    bool      use_xsdt;
    apic_info_t apic;
} acpi_t;

/*
 * Initialization and table access
 */
bool acpi_init(u64 rsdp_addr);
acpi_t *acpi_get_table(void);
u8 acpi_checksum(void *table, u32 length);

/*
 * Table finding (from acpifnc.c)
 */
void *acpi_find_table(const char *signature);
void *acpi_find_table_with_index(const char *signature, int index);
bool acpi_parse_madt(void);
u32 acpi_get_local_apic_addr(void);
bool acpi_enable_local_apic(void);

/*
 * Power management (from acpipwr.c)
 */
void acpi_reboot(void);
void acpi_shutdown(void);
void acpi_sleep(u8 sleep_type);

#endif