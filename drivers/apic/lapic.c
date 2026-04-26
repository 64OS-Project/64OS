#include <apic.h>
#include <asm/mmio.h>
#include <kernel/driver.h>
#include <asm/cpu.h>
#include <acpi.h>

static struct {
    u32 base;
    volatile void *virt;
    bool enabled;
} g_apic = {0};

static int apic_probe(driver_t *drv) {
    (void)drv;
    return 0;  /*
 * We always say that the driver is suitable
 */
}

static int apic_init_driver(driver_t *drv) {
    (void)drv;
    
    /*
 * The APIC should already be initialized via acpi_get_local_apic_addr()
        but if not, we use the backup address.
 */
    if (!g_apic.base) {
        u32 lapic_addr = acpi_get_local_apic_addr();
	if (!lapic_addr) {
	    lapic_addr = 0xFEE00000;  /*
 * APIC Standard Adment
 */
	}
        if (!apic_init(lapic_addr)) {
            return -1;
        }
    }
    
    apic_enable();
    
    return 0;
}

static void apic_remove(driver_t *drv) {
    (void)drv;
    apic_disable();
}

driver_t g_lapic_driver = {
    .name = "lapic",
    .desc = "Advanced Programmable Interrupt Controller Driver",
    .critical_level = DRIVER_CRITICAL_0,
    .probe = apic_probe,
    .init = apic_init_driver,
    .remove = apic_remove,
};

bool apic_init(u32 base_addr) {
    if (!base_addr) return false;
    
    g_apic.base = base_addr;
    g_apic.virt = (volatile void*)(uptr)base_addr;
    g_apic.enabled = false;
    
    /*
 * Check version
 */
    u32 ver = apic_read_reg(LAPIC_VERSION);
    if ((ver & 0xFF) == 0) return false;
    
    return true;
}

u32 apic_read_reg(u32 reg) {
    if (!g_apic.virt) return 0;
    return mmio_read32(g_apic.virt + reg);
}

void apic_write_reg(u32 reg, u32 val) {
    if (!g_apic.virt) return;
    mmio_write32(g_apic.virt + reg, val);
}

void apic_enable(void) {
    if (!g_apic.base) return;
    
    u32 svr = apic_read_reg(LAPIC_SVR);
    svr |= (1 << 8);          /*
 * Enable
 */
    svr &= ~0xFF;
    svr |= 0xFF;               /*
 * Spurious vector
 */
    apic_write_reg(LAPIC_SVR, svr);

    apic_write_reg(LAPIC_LVT_LINT0, LVT_MASKED);
    apic_write_reg(LAPIC_LVT_LINT1, LVT_MASKED);

    apic_write_reg(LAPIC_TPR, 0);
    
    g_apic.enabled = true;
}

void apic_disable(void) {
    if (!g_apic.base) return;
    
    u32 svr = apic_read_reg(LAPIC_SVR);
    svr &= ~(1 << 8);
    apic_write_reg(LAPIC_SVR, svr);
    
    g_apic.enabled = false;
}

void apic_eoi(void) {
    if (!g_apic.enabled) return;
    apic_write_reg(LAPIC_EOI, 0);
}

u32 apic_get_id(void) {
    u32 id = apic_read_reg(LAPIC_ID);
    return (id >> 24) & 0xFF;
}

u32 apic_get_version(void) {
    return apic_read_reg(LAPIC_VERSION) & 0xFF;
}

void apic_send_ipi(u32 apic_id, u32 vector) {
    while (apic_read_reg(LAPIC_ICR_LOW) & (1 << 12));
    
    apic_write_reg(LAPIC_ICR_HIGH, apic_id << 24);
    apic_write_reg(LAPIC_ICR_LOW, vector | DELIVERY_FIXED);
}

void apic_send_broadcast(u32 vector) {
    while (apic_read_reg(LAPIC_ICR_LOW) & (1 << 12));
    
    apic_write_reg(LAPIC_ICR_LOW, vector | DELIVERY_FIXED | ICR_DEST_ALL);
}

void apic_send_init(u32 apic_id) {
    /*
 * We are waiting for the ICR to be ready.
 */
    int timeout = 10000;
    while ((apic_read_reg(LAPIC_ICR_LOW) & (1 << 12)) && timeout--) {
        asm volatile("pause");
    }
    if (timeout == 0) return;
    
    apic_write_reg(LAPIC_ICR_HIGH, apic_id << 24);
    /*
 * DELIVERY_INIT = 5, LEVEL_ASSERT = 1
 */
    apic_write_reg(LAPIC_ICR_LOW, (5 << 8) | (1 << 14) | ICR_DEST_PHYSICAL);
    
    /*
 * We are waiting for completion
 */
    timeout = 10000;
    while ((apic_read_reg(LAPIC_ICR_LOW) & (1 << 12)) && timeout--) {
        asm volatile("pause");
    }
}


void apic_send_startup(u32 apic_id, u32 vector) {
    /*
 * e are waiting for the ICR to be ready.
 */
    int timeout = 10000;
    while ((apic_read_reg(LAPIC_ICR_LOW) & (1 << 12)) && timeout--) {
        asm volatile("pause");
    }
    if (timeout == 0) return;
    
    apic_write_reg(LAPIC_ICR_HIGH, apic_id << 24);
    /*
 * DELIVERY_STARTUP = 6, vector in least significant bits
 */
    apic_write_reg(LAPIC_ICR_LOW, (vector & 0xFF) | (6 << 8) | ICR_DEST_PHYSICAL);
    
    /*
 * We are waiting for completion
 */
    timeout = 10000;
    while ((apic_read_reg(LAPIC_ICR_LOW) & (1 << 12)) && timeout--) {
        asm volatile("pause");
    }
}
