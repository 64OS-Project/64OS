#include <acpi.h>
#include <libk/string.h>
#include <asm/io.h>
#include <asm/mmio.h>
#include <kernel/driver.h>
#include <mbstruct.h>

acpi_t g_acpi = {0};

extern multiboot2_info_t mbinfo;

static int acpi_probe(driver_t *drv) {
    (void)drv;
    if (mbinfo.acpi.rsdpv1_addr || mbinfo.acpi.rsdpv2_addr) { return 0; }
    return 1;
}

static int acpi_init_driver(driver_t *drv) {
    (void)drv;
    return acpi_init(g_acpi.rsdp ? (u64)(uptr)g_acpi.rsdp : 0);
}

driver_t g_acpi_driver = {
    .name = "acpi",
    .desc = "Advanced Configuration and Power Interface Driver",
    .critical_level = DRIVER_CRITICAL_0,
    .probe = acpi_probe,
    .init = acpi_init_driver,
    .remove = NULL // ACPI cannot be removed
};

/*
 * ============================================================================= Internal Utilities ============================================================================
 */

u8 acpi_checksum(void *table, u32 length) {
    u8 sum = 0;
    u8 *bytes = (u8*)table;
    for (u32 i = 0; i < length; i++) {
        sum += bytes[i];
    }
    return sum;
}

static bool acpi_validate_rsdp(rsdp_v2_t *rsdp) {
    if (memcmp(rsdp->v1.signature, ACPI_RSDP_SIGNATURE, 8) != 0) {
        return false;
    }
    
    if (acpi_checksum(rsdp, ACPI_RSDP_V1_SIZE) != 0) {
        return false;
    }
    
    if (rsdp->v1.revision >= 2) {
        if (rsdp->length > sizeof(rsdp_v2_t)) {
            return false;
        }
        if (acpi_checksum(rsdp, rsdp->length) != 0) {
            return false;
        }
        g_acpi.use_xsdt = true;
    }
    
    return true;
}

/*
 * ============================================================================= Table Finding Functions ============================================================================
 */

static void *acpi_find_rsdt(const char *signature) {
    if (!g_acpi.rsdt) return NULL;
    
    u32 entries_count = (g_acpi.rsdt->header.length - sizeof(sdt_header_t)) / 4;
    u32 *entries = (u32*)((uptr)g_acpi.rsdt + sizeof(sdt_header_t));
    
    for (u32 i = 0; i < entries_count; i++) {
        sdt_header_t *header = (sdt_header_t*)(uptr)entries[i];
        if (memcmp(header->signature, signature, 4) == 0) {
            if (acpi_checksum(header, header->length) == 0) {
                return header;
            }
        }
    }
    
    return NULL;
}

static void *acpi_find_xsdt(const char *signature) {
    if (!g_acpi.xsdt) return NULL;
    
    u32 entries_count = (g_acpi.xsdt->header.length - sizeof(sdt_header_t)) / 8;
    u64 *entries = (u64*)((uptr)g_acpi.xsdt + sizeof(sdt_header_t));
    
    for (u32 i = 0; i < entries_count; i++) {
        sdt_header_t *header = (sdt_header_t*)(uptr)entries[i];
        if (memcmp(header->signature, signature, 4) == 0) {
            if (acpi_checksum(header, header->length) == 0) {
                return header;
            }
        }
    }
    
    return NULL;
}

void *acpi_find_table(const char *signature) {
    return acpi_find_table_with_index(signature, 0);
}

void *acpi_find_table_with_index(const char *signature, int index) {
    if (!g_acpi.rsdt && !g_acpi.xsdt) return NULL;
    
    int found = 0;
    
    if (g_acpi.use_xsdt) {
        u32 entries_count = (g_acpi.xsdt->header.length - sizeof(sdt_header_t)) / 8;
        u64 *entries = (u64*)((uptr)g_acpi.xsdt + sizeof(sdt_header_t));
        
        for (u32 i = 0; i < entries_count; i++) {
            sdt_header_t *header = (sdt_header_t*)(uptr)entries[i];
            if (memcmp(header->signature, signature, 4) == 0) {
                if (found == index) {
                    if (acpi_checksum(header, header->length) == 0) {
                        return header;
                    }
                }
                found++;
            }
        }
    } else {
        u32 entries_count = (g_acpi.rsdt->header.length - sizeof(sdt_header_t)) / 4;
        u32 *entries = (u32*)((uptr)g_acpi.rsdt + sizeof(sdt_header_t));
        
        for (u32 i = 0; i < entries_count; i++) {
            sdt_header_t *header = (sdt_header_t*)(uptr)entries[i];
            if (memcmp(header->signature, signature, 4) == 0) {
                if (found == index) {
                    if (acpi_checksum(header, header->length) == 0) {
                        return header;
                    }
                }
                found++;
            }
        }
    }
    
    return NULL;
}

/*
 * ============================================================================= MADT Parsing ============================================================================
 */

bool acpi_parse_madt(void) {
    if (!g_acpi.madt) return false;
    
    apic_info_t *apic = &g_acpi.apic;
    memset(apic, 0, sizeof(apic_info_t));
    
    apic->local_apic_address = g_acpi.madt->local_apic_address;
    
    u8 *entry = (u8*)g_acpi.madt + sizeof(madt_t);
    u8 *end = (u8*)g_acpi.madt + g_acpi.madt->header.length;
    
    while (entry < end) {
        madt_entry_header_t *header = (madt_entry_header_t*)entry;
        
        switch (header->type) {
            case MADT_TYPE_LOCAL_APIC: {
                madt_local_apic_t *lapic = (madt_local_apic_t*)entry;
                if (apic->processor_count < 64) {
                    apic->processors[apic->processor_count].acpi_id = lapic->acpi_processor_id;
                    apic->processors[apic->processor_count].apic_id = lapic->apic_id;
                    apic->processors[apic->processor_count].enabled = (lapic->flags & 1);
                    apic->processor_count++;
                }
                break;
            }
            
            case MADT_TYPE_IO_APIC: {
                madt_io_apic_t *ioapic = (madt_io_apic_t*)entry;
                if (apic->io_apic_count < 16) {
                    apic->io_apics[apic->io_apic_count].address = ioapic->io_apic_address;
                    apic->io_apics[apic->io_apic_count].gsi_base = ioapic->global_system_interrupt_base;
                    apic->io_apic_count++;
                }
                break;
            }
            
            case MADT_TYPE_INT_SOURCE_OVERRIDE: {
                madt_int_source_override_t *override = (madt_int_source_override_t*)entry;
                if (apic->int_override_count < 16) {
                    apic->int_overrides[apic->int_override_count].bus = override->bus;
                    apic->int_overrides[apic->int_override_count].source = override->source;
                    apic->int_overrides[apic->int_override_count].gsi = override->global_system_interrupt;
                    apic->int_overrides[apic->int_override_count].flags = override->flags;
                    apic->int_override_count++;
                }
                break;
            }
        }
        
        entry += header->length;
    }
    
    return true;
}

u32 acpi_get_local_apic_addr(void) {
    return g_acpi.apic.local_apic_address;
}

bool acpi_enable_local_apic(void) {
    u32 apic_addr = g_acpi.apic.local_apic_address;
    if (!apic_addr) return false;
    
    u32 svr = mmio_read32((volatile void*)(uptr)(apic_addr + 0xF0));
    svr |= (1 << 8);
    svr = (svr & 0xFFFFFF00) | 0xFF;
    mmio_write32((volatile void*)(uptr)(apic_addr + 0xF0), svr);
    
    return true;
}

/*
 * ============================================================================= Initialization ============================================================================
 */

bool acpi_init(u64 rsdp_addr) {
    if (!rsdp_addr) return false;
    
    memset(&g_acpi, 0, sizeof(g_acpi));
    
    g_acpi.rsdp = (rsdp_v2_t*)(uptr)rsdp_addr;
    if (!acpi_validate_rsdp(g_acpi.rsdp)) {
        return false;
    }
    
    if (g_acpi.use_xsdt) {
        g_acpi.xsdt = (xsdt_t*)(uptr)g_acpi.rsdp->xsdt_address;
        if (acpi_checksum(g_acpi.xsdt, g_acpi.xsdt->header.length) != 0) {
            return false;
        }
    } else {
        g_acpi.rsdt = (rsdt_t*)(uptr)g_acpi.rsdp->v1.rsdt_address;
        if (acpi_checksum(g_acpi.rsdt, g_acpi.rsdt->header.length) != 0) {
            return false;
        }
    }
    
    g_acpi.fadt = (fadt_t*)acpi_find_table("FACP");
    g_acpi.madt = (madt_t*)acpi_find_table("APIC");
    g_acpi.hpet = (hpet_t*)acpi_find_table("HPET");
    g_acpi.mcfg = (mcfg_t*)acpi_find_table("MCFG");
    
    if (g_acpi.madt) {
        acpi_parse_madt();
    }
    
    return true;
}

acpi_t *acpi_get_table(void) {
    return &g_acpi;
}
