#ifndef ACPITABLES_H
#define ACPITABLES_H

#include <kernel/types.h>

/*
 * ACPI signatures
 */
#define ACPI_RSDP_SIGNATURE      "RSD PTR "
#define ACPI_RSDP_V1_SIZE        20
#define ACPI_RSDP_V2_SIZE        36

/*
 * MADT entry types
 */
#define MADT_TYPE_LOCAL_APIC              0
#define MADT_TYPE_IO_APIC                  1
#define MADT_TYPE_INT_SOURCE_OVERRIDE       2
#define MADT_TYPE_NMI_SOURCE                3
#define MADT_TYPE_LOCAL_APIC_NMI            4
#define MADT_TYPE_LOCAL_APIC_ADDR_OVERRIDE  5
#define MADT_TYPE_IO_SAPIC                  6
#define MADT_TYPE_LOCAL_SAPIC               7
#define MADT_TYPE_PLATFORM_INT_SOURCE       8
#define MADT_TYPE_PROCESSOR_LOCAL_X2APIC    9
#define MADT_TYPE_LOCAL_X2APIC_NMI          10

/*
 * ACPI System Descriptor Table Header
 */
typedef struct {
    char     signature[4];
    u32    length;
    u8     revision;
    u8     checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    u32    oem_revision;
    u32    creator_id;
    u32    creator_revision;
} __attribute__((packed)) sdt_header_t;

/*
 * RSDP (v1 and v2)
 */
typedef struct {
    char     signature[8];
    u8     checksum;
    char     oem_id[6];
    u8     revision;
    u32    rsdt_address;
} __attribute__((packed)) rsdp_v1_t;

typedef struct {
    rsdp_v1_t v1;
    u32    length;
    u64    xsdt_address;
    u8     extended_checksum;
    u8     reserved[3];
} __attribute__((packed)) rsdp_v2_t;

/*
 * RSDT (Root System Description Table)
 */
typedef struct {
    sdt_header_t header;
    u32        entries[];
} __attribute__((packed)) rsdt_t;

/*
 * XSDT (Extended System Description Table)
 */
typedef struct {
    sdt_header_t header;
    u64        entries[];
} __attribute__((packed)) xsdt_t;

/*
 * FADT (Fixed ACPI Description Table)
 */
typedef struct {
    sdt_header_t header;
    u32        firmware_ctrl;
    u32        dsdt;
    u8         reserved;
    u8         preferred_pm_profile;
    u16        sci_int;
    u32        smi_cmd;
    u8         acpi_enable;
    u8         acpi_disable;
    u8         s4bios_req;
    u8         pstate_cnt;
    u32        pm1a_evt_blk;
    u32        pm1b_evt_blk;
    u32        pm1a_cnt_blk;
    u32        pm1b_cnt_blk;
    u32        pm2_cnt_blk;
    u32        pm_tmr_blk;
    u32        gpe0_blk;
    u32        gpe1_blk;
    u8         pm1_evt_len;
    u8         pm1_cnt_len;
    u8         pm2_cnt_len;
    u8         pm_tmr_len;
    u8         gpe0_blk_len;
    u8         gpe1_blk_len;
    u8         gpe1_base;
    u8         cst_cnt;
    u16        p_lvl2_lat;
    u16        p_lvl3_lat;
    u16        flush_size;
    u16        flush_stride;
    u8         duty_offset;
    u8         duty_width;
    u8         day_alrm;
    u8         mon_alrm;
    u8         century;
    u16        iapc_boot_arch;
    u8         reserved2;
    u32        flags;
    u8         reset_reg[12];      /*
 * Generic Address Structure
 */
    u8         reset_value;
    u8         reserved3[3];
    u64        x_firmware_ctrl;
    u64        x_dsdt;
    u8         x_pm1a_evt_blk[12];
    u8         x_pm1b_evt_blk[12];
    u8         x_pm1a_cnt_blk[12];
    u8         x_pm1b_cnt_blk[12];
    u8         x_pm2_cnt_blk[12];
    u8         x_pm_tmr_blk[12];
    u8         x_gpe0_blk[12];
    u8         x_gpe1_blk[12];
} __attribute__((packed)) fadt_t;

/*
 * MADT (Multiple APIC Description Table)
 */
typedef struct {
    sdt_header_t header;
    u32        local_apic_address;
    u32        flags;
} __attribute__((packed)) madt_t;

/*
 * MADT Entry Header
 */
typedef struct {
    u8 type;
    u8 length;
} __attribute__((packed)) madt_entry_header_t;

/*
 * MADT Local APIC Entry
 */
typedef struct {
    madt_entry_header_t header;
    u8  acpi_processor_id;
    u8  apic_id;
    u32 flags;
} __attribute__((packed)) madt_local_apic_t;

/*
 * MADT IO APIC Entry
 */
typedef struct {
    madt_entry_header_t header;
    u8  io_apic_id;
    u8  reserved;
    u32 io_apic_address;
    u32 global_system_interrupt_base;
} __attribute__((packed)) madt_io_apic_t;

/*
 * MADT Interrupt Source Override
 */
typedef struct {
    madt_entry_header_t header;
    u8  bus;
    u8  source;
    u32 global_system_interrupt;
    u16 flags;
} __attribute__((packed)) madt_int_source_override_t;

/*
 * HPET (High Precision Event Timer)
 */
typedef struct {
    sdt_header_t header;
    u32        event_timer_block_id;
    u8         base_address[12];  /*
 * Generic Address Structure
 */
    u8         hpet_number;
    u16        minimum_tick;
    u8         page_protection;
} __attribute__((packed)) hpet_t;

/*
 * MCFG (PCI Express Memory Mapped Configuration)
 */
typedef struct {
    sdt_header_t header;
    u64        reserved;
} __attribute__((packed)) mcfg_t;

typedef struct {
    u64        base_address;
    u16        pci_segment_group;
    u8         start_bus;
    u8         end_bus;
    u32        reserved;
} __attribute__((packed)) mcfg_entry_t;

#endif
