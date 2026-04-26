#include <usb/ehci.h>
#include <pci.h>
#include <asm/mmio.h>
#include <asm/io.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <kernel/timer.h>
#include <kernel/paging.h>
#include <ioapic.h>
#include <apic.h>
#include <idt.h>

static ehci_context_t *g_ehci = NULL;
extern pmm_t pmm;

static int ehci_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup,
                                  u8 *data, u32 data_len);
static void ehci_scan_ports(ehci_context_t *ctx);
static int ehci_controller_init(ehci_context_t *ctx);

// ==================== MMIO ACCESS ====================

static inline u32 ehci_read_cap(ehci_context_t *ctx, u32 reg) {
    return mmio_read32((volatile void*)((uptr)ctx->cap_regs + reg));
}

static inline void ehci_write_cap(ehci_context_t *ctx, u32 reg, u32 val) {
    mmio_write32((volatile void*)((uptr)ctx->cap_regs + reg), val);
}

static inline u32 ehci_read_op(ehci_context_t *ctx, u32 reg) {
    return mmio_read32((volatile void*)((uptr)ctx->op_regs + reg));
}

static inline void ehci_write_op(ehci_context_t *ctx, u32 reg, u32 val) {
    mmio_write32((volatile void*)((uptr)ctx->op_regs + reg), val);
}

// ==================== QH/QTD MANAGEMENT ====================

static ehci_qh_t *ehci_alloc_qh(void) {
    // QH must be 32-byte aligned
    u8 *virt = (u8*)pmm_alloc_aligned_range(&pmm, 1, 8);
    if (!virt) return NULL;
    
    memset(virt, 0, PAGE_SIZE);
    return (ehci_qh_t*)virt;
}

static ehci_qtd_t *ehci_alloc_qtd(void) {
    u8 *virt = (u8*)pmm_alloc_aligned_range(&pmm, 1, 8);
    if (!virt) return NULL;
    
    memset(virt, 0, PAGE_SIZE);
    return (ehci_qtd_t*)virt;
}

static void ehci_free_qh(ehci_qh_t *qh) {
    if (qh) {
        pmm_free_page(&pmm, (void*)qh);
    }
}

static void ehci_free_qtd(ehci_qtd_t *qtd) {
    if (qtd) {
        pmm_free_page(&pmm, (void*)qtd);
    }
}

static void ehci_qh_setup(ehci_qh_t *qh, u8 dev_addr, u8 ep_num, u8 ep_speed, u16 max_packet) {
    memset(qh, 0, sizeof(ehci_qh_t));
    
    // Terminate horizontal link
    qh->horizontal_link = EHCI_LINK_TERMINATE;
    
    // Endpoint capabilities
    qh->endpoint_caps = (dev_addr << EHCI_QH_DEV_ADDR_SHIFT);
    qh->endpoint_caps |= (ep_num << EHCI_QH_EP_NUM_SHIFT);
    qh->endpoint_caps |= EHCI_QH_DTC;  // Data toggle control
    
    switch (ep_speed) {
        case 0: qh->endpoint_caps |= EHCI_QH_EP_SPEED_FULL; break;
        case 1: qh->endpoint_caps |= EHCI_QH_EP_SPEED_LOW; break;
        case 2: qh->endpoint_caps |= EHCI_QH_EP_SPEED_HIGH; break;
    }
    
    qh->endpoint_caps |= (max_packet << EHCI_QH_MAX_PKT_SHIFT);
    
    // Terminate next qTD
    qh->next_qtd = EHCI_LINK_TERMINATE;
    qh->alt_next_qtd = EHCI_LINK_TERMINATE;
}

static void ehci_qtd_setup(ehci_qtd_t *qtd, u8 pid, u32 length, u32 buffer_phys) {
    memset(qtd, 0, sizeof(ehci_qtd_t));
    
    // Terminate links
    qtd->horizontal_link = EHCI_LINK_TERMINATE;
    qtd->alt_next_qtd = EHCI_LINK_TERMINATE;
    
    // Setup token
    qtd->token = EHCI_QTD_ACTIVE;
    qtd->token |= (pid << 8);
    qtd->token |= (length << 16);
    
    // Setup buffers (5 pages max, 4KB each)
    u32 remaining = length;
    u32 offset = 0;
    
    for (int i = 0; i < 5 && remaining > 0; i++) {
        u32 page = (buffer_phys + offset) & ~0xFFF;
        qtd->buffer[i] = page;
        offset += 4096;
        remaining = (remaining > 4096) ? remaining - 4096 : 0;
    }
}

// ==================== CONTROL TRANSFER ====================

static int ehci_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup,
                                  u8 *data, u32 data_len) {
    if (!g_ehci || !dev) return -1;
    
    // Allocate QH for control endpoint
    ehci_qh_t *qh = ehci_alloc_qh();
    if (!qh) return -1;
    
    ehci_qh_setup(qh, dev->address, 0, dev->speed, dev->max_packet_size0);
    
    // Allocate qTDs
    ehci_qtd_t *setup_qtd = ehci_alloc_qtd();
    ehci_qtd_t *data_qtd = NULL;
    ehci_qtd_t *status_qtd = ehci_alloc_qtd();
    
    if (!setup_qtd || !status_qtd) {
        ehci_free_qh(qh);
        if (setup_qtd) ehci_free_qtd(setup_qtd);
        if (status_qtd) ehci_free_qtd(status_qtd);
        return -1;
    }
    
    // Setup stage
    u32 setup_phys = (u32)(uptr)setup;
    ehci_qtd_setup(setup_qtd, EHCI_PID_SETUP, sizeof(usb_setup_packet_t), setup_phys);
    
    // Link setup -> status (initially)
    setup_qtd->horizontal_link = (u32)(uptr)status_qtd;
    
    // Data stage (if any)
    if (data_len > 0 && data) {
        data_qtd = ehci_alloc_qtd();
        if (!data_qtd) {
            ehci_free_qh(qh);
            ehci_free_qtd(setup_qtd);
            ehci_free_qtd(status_qtd);
            return -1;
        }
        
        u8 pid = (setup->bmRequestType & USB_DIR_IN) ? EHCI_PID_IN : EHCI_PID_OUT;
        u32 data_phys = (u32)(uptr)data;
        ehci_qtd_setup(data_qtd, pid, data_len, data_phys);
        
        // Re-link: setup -> data -> status
        setup_qtd->horizontal_link = (u32)(uptr)data_qtd;
        data_qtd->horizontal_link = (u32)(uptr)status_qtd;
    }
    
    // Status stage (opposite direction)
    u8 status_pid = (setup->bmRequestType & USB_DIR_IN) ? EHCI_PID_OUT : EHCI_PID_IN;
    ehci_qtd_setup(status_qtd, status_pid, 0, 0);
    
    // Link QH to qTD chain
    qh->next_qtd = (u32)(uptr)setup_qtd;
    
    // Add QH to async schedule
    u32 old_link = g_ehci->async_qh->horizontal_link;
    g_ehci->async_qh->horizontal_link = (u32)(uptr)qh;
    
    // Wait for completion
    int timeout = 10000;  // 1 second
    u32 token = status_qtd->token;
    
    while (timeout-- && (token & EHCI_QTD_ACTIVE)) {
        token = status_qtd->token;
        timer_udelay(100);
    }
    
    // Remove QH from schedule
    g_ehci->async_qh->horizontal_link = old_link;
    
    // Check result
    int ret = 0;
    if (token & EHCI_QTD_HALTED) {
        terminal_error_printf("[EHCI] Control transfer failed: token=0x%x\n", token);
        ret = -1;
    }
    
    // Cleanup
    ehci_free_qtd(setup_qtd);
    if (data_qtd) ehci_free_qtd(data_qtd);
    ehci_free_qtd(status_qtd);
    ehci_free_qh(qh);
    
    return ret;
}

// ==================== PORT MANAGEMENT ====================

static void ehci_reset_port(ehci_context_t *ctx, u32 port) {
    u32 portsc = ehci_read_op(ctx, EHCI_PORTSC(port));
    
    // Reset port
    portsc |= EHCI_PORT_RESET;
    ehci_write_op(ctx, EHCI_PORTSC(port), portsc);
    
    timer_mdelay(10);
    
    portsc &= ~EHCI_PORT_RESET;
    ehci_write_op(ctx, EHCI_PORTSC(port), portsc);
    
    timer_mdelay(10);
    
    // Enable port
    portsc = ehci_read_op(ctx, EHCI_PORTSC(port));
    portsc |= EHCI_PORT_PE;
    ehci_write_op(ctx, EHCI_PORTSC(port), portsc);
}

static void ehci_scan_ports(ehci_context_t *ctx) {
    for (u32 i = 0; i < ctx->ports; i++) {
        u32 portsc = ehci_read_op(ctx, EHCI_PORTSC(i));

        terminal_printf("[EHCI] Port %d: status=0x%08x\n", i, portsc);
        
        // Clear change bits
        if (portsc & EHCI_PORT_CSC) {
	    terminal_printf("[EHCI] Port %d: connect status change\n", i);
            ehci_write_op(ctx, EHCI_PORTSC(i), portsc | EHCI_PORT_CSC);
        }
        if (portsc & EHCI_PORT_PEC) {
            terminal_printf("[EHCI] Port %d: port enable change\n", i);
            ehci_write_op(ctx, EHCI_PORTSC(i), portsc | EHCI_PORT_PEC);
        }
        
        bool connected = (portsc & EHCI_PORT_CCS) != 0;
        
        if (connected && !ctx->ports_info[i].connected) {
            // Device connected
            terminal_printf("[EHCI] Port %d: device connected\n", i);
            
            ctx->ports_info[i].connected = true;
            ctx->ports_info[i].speed = (portsc & EHCI_PORT_SPEED_MASK) >> 26;
            
            // Reset port
            ehci_reset_port(ctx, i);
            
            // Create and enumerate device
            usb_device_t *dev = usb_device_alloc(ctx->hcd);
            if (dev) {
                dev->address = 0;
                dev->speed = ctx->ports_info[i].speed;
                dev->max_packet_size0 = 8;  // Default, will be updated
                dev->hcd = ctx->hcd;
                
                if (usb_enumeration(dev) == 0) {
                    usb_device_add(ctx->hcd, dev);
                    ctx->ports_info[i].device = dev;
                } else {
                    usb_device_free(dev);
                }
            }
        } else if (!connected && ctx->ports_info[i].connected) {
            // Device disconnected
            terminal_printf("[EHCI] Port %d: device disconnected\n", i);
            
            if (ctx->ports_info[i].device) {
                usb_device_remove(ctx->ports_info[i].device);
                ctx->ports_info[i].device = NULL;
            }
            
            ctx->ports_info[i].connected = false;
        }
    }
}

// ==================== INTERRUPTS ====================

void ehci_irq_handler(usb_hcd_t *hcd) {
    ehci_context_t *ctx = (ehci_context_t*)hcd->private;
    if (!ctx) return;
    
    u32 status = ehci_read_op(ctx, EHCI_USBSTS);
    
    // Clear interrupts
    ehci_write_op(ctx, EHCI_USBSTS, status);
    
    if (status & EHCI_STS_PORT_CHANGE) {
        ehci_scan_ports(ctx);
    }
    
    if (status & EHCI_STS_USBINT) {
        // Transfer complete
    }
    
    if (status & EHCI_STS_ERROR) {
        terminal_warn_printf("[EHCI] Error interrupt: 0x%x\n", status);
    }
    
    // Send EOI
    ioapic_eoi(ctx->gsi);
}

static int ehci_setup_irq(ehci_context_t *ctx, pci_device_t *pci_dev) {
    ctx->irq = pci_dev->interrupt_line;
    
    if (!ioapic_get_override(ctx->irq, &ctx->gsi, NULL)) {
        ctx->gsi = ctx->irq;
    }
    
    ctx->vector = 48 + (ctx->gsi % 16);
    
    if (!ioapic_redirect_irq(ctx->gsi, ctx->vector, apic_get_id(),
                              IOAPIC_FLAG_EDGE_TRIGGERED | IOAPIC_FLAG_ACTIVE_HIGH)) {
        terminal_error_printf("[EHCI] Failed to redirect IRQ %d (GSI %d)\n",
                              ctx->irq, ctx->gsi);
        return -1;
    }
    
    // Register IRQ handler (simplified)
    idt_set_gate(ctx->vector, (void*)ehci_irq_handler, KERNEL_CODE_SEL, IDT_GATE_INT, 0);
    ioapic_unmask_irq(ctx->gsi);
    
    terminal_printf("[EHCI] IRQ %d (GSI %d) -> vector %d\n",
                    ctx->irq, ctx->gsi, ctx->vector);
    
    return 0;
}

// ==================== EHCI INITIALIZATION ====================

static int ehci_controller_init(ehci_context_t *ctx) {
    terminal_printf("[EHCI] Initializing controller...\n");
    
    // Reset controller
    ehci_write_op(ctx, EHCI_USBCMD, EHCI_CMD_HCRESET);
    
    int timeout = 10000;
    while (timeout-- && (ehci_read_op(ctx, EHCI_USBCMD) & EHCI_CMD_HCRESET)) {
        timer_udelay(100);
    }
    
    if (timeout == 0) {
        terminal_error_printf("[EHCI] Reset timeout\n");
        return -1;
    }
    
    // Get parameters
    ctx->hcsparams = ehci_read_cap(ctx, EHCI_HCSPARAMS);
    ctx->ports = EHCI_HCSPARAMS_NPORTS(ctx->hcsparams);
    ctx->companion_ports = (ctx->hcsparams >> 8) & 0xFF;
    
    terminal_printf("[EHCI] Ports: %d, Companion mask: 0x%x\n", 
                    ctx->ports, ctx->companion_ports);
    
    // Allocate periodic frame list (4KB)
    ctx->periodic_list = (u32*)pmm_alloc_aligned_range(&pmm, 1, 1);
    if (!ctx->periodic_list) return -1;
    ctx->periodic_list_phys = (u32)(uptr)ctx->periodic_list;
    memset(ctx->periodic_list, 0, PAGE_SIZE);
    
    // Initialize frame list entries (all terminated)
    for (int i = 0; i < 1024; i++) {
        ctx->periodic_list[i] = EHCI_LINK_TERMINATE;
    }
    
    // Allocate async QH
    ctx->async_qh = ehci_alloc_qh();
    if (!ctx->async_qh) {
        pmm_free_page(&pmm, ctx->periodic_list);
        return -1;
    }
    
    // Setup async QH (high speed control/bulk default)
    ehci_qh_setup(ctx->async_qh, 0, 0, 2, 8);
    ctx->async_qh->horizontal_link = EHCI_LINK_TERMINATE;
    
    // Set base addresses
    ehci_write_op(ctx, EHCI_PERIODICLISTBASE, ctx->periodic_list_phys);
    ehci_write_op(ctx, EHCI_ASYNCLISTADDR, (u32)(uptr)ctx->async_qh);
    
    // Enable schedules
    ehci_write_op(ctx, EHCI_USBCMD, EHCI_CMD_RS | EHCI_CMD_ASE | EHCI_CMD_PSE);
    
    // Enable interrupts
    ehci_write_op(ctx, EHCI_USBINTR, EHCI_INTR_PORT_CHANGE | EHCI_INTR_USB | EHCI_INTR_ERROR);
    
    // Set config flag (take ownership from BIOS)
    ehci_write_op(ctx, EHCI_CONFIGFLAG, 1);
    
    // Wait for run
    timer_mdelay(10);
    
    // Check if running
    if (ehci_read_op(ctx, EHCI_USBSTS) & EHCI_STS_HALT) {
        terminal_error_printf("[EHCI] Controller halted\n");
        return -1;
    }
    
    ctx->running = true;
    terminal_success_printf("[EHCI] Controller running\n");
    
    return 0;
}

// ==================== HCD CALLBACKS ====================

static int ehci_hcd_init(usb_hcd_t *hcd) {
    ehci_context_t *ctx = (ehci_context_t*)hcd->private;
    if (!ctx) return -1;
    
    int ret = ehci_controller_init(ctx);
    if (ret != 0) return ret;
    
    // Initial port scan
    ehci_scan_ports(ctx);
    
    return 0;
}

static int ehci_hcd_submit_transfer(usb_hcd_t *hcd, usb_transfer_t *transfer) {
    if (!hcd || !transfer || !transfer->device) return -1;
    
    switch (transfer->type) {
        case USB_TRANSFER_CONTROL:
            return ehci_control_transfer(transfer->device,
                                         (usb_setup_packet_t*)transfer->context,
                                         transfer->buffer, transfer->length);
        default:
            return -1;
    }
}

static int ehci_hcd_shutdown(usb_hcd_t *hcd) {
    ehci_context_t *ctx = (ehci_context_t*)hcd->private;
    if (!ctx) return 0;
    
    // Stop controller
    ehci_write_op(ctx, EHCI_USBCMD, 0);
    ctx->running = false;
    
    // Disable interrupts
    ehci_write_op(ctx, EHCI_USBINTR, 0);
    
    // Free resources
    if (ctx->async_qh) ehci_free_qh(ctx->async_qh);
    if (ctx->periodic_list) pmm_free_page(&pmm, ctx->periodic_list);
    
    terminal_printf("[EHCI] Shutdown complete\n");
}

static usb_hcd_ops_t ehci_hcd_ops = {
    .init = ehci_hcd_init,
    .shutdown = ehci_hcd_shutdown,
    .reset = NULL,
    .submit_transfer = ehci_hcd_submit_transfer,
    .cancel_transfer = NULL,
    .set_address = NULL,
    .enable_endpoint = NULL,
    .disable_endpoint = NULL,
    .irq_handler = ehci_irq_handler,
};

// ==================== PCI DETECTION ====================

int ehci_init(pci_device_t *pci_dev) {
    if (!pci_dev) return -1;
    
    terminal_printf("[EHCI] Found EHCI controller at %02X:%02X.%X\n",
                    pci_dev->bus, pci_dev->slot, pci_dev->function);
    
    // Enable bus mastering
    pci_enable_busmaster(pci_dev);
    pci_enable(pci_dev);
    
    // Get MMIO base (BAR0)
    u32 mmio_base = pci_dev->bars[0] & ~0xF;
    if (!mmio_base) {
        terminal_error_printf("[EHCI] No MMIO BAR\n");
        return -1;
    }
    
    // Allocate context
    ehci_context_t *ctx = (ehci_context_t*)malloc(sizeof(ehci_context_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(ehci_context_t));
    
    ctx->cap_regs = (volatile void*)(uptr)mmio_base;
    ctx->caplength = mmio_read8(ctx->cap_regs);
    ctx->op_regs = (volatile void*)((uptr)ctx->cap_regs + ctx->caplength);
    
    // Create HCD
    usb_hcd_t *hcd = (usb_hcd_t*)malloc(sizeof(usb_hcd_t));
    if (!hcd) {
        free(ctx);
        return -1;
    }
    
    memset(hcd, 0, sizeof(usb_hcd_t));
    snprintf(hcd->name, sizeof(hcd->name), "ehci");
    hcd->vendor_id = pci_dev->vendor_id;
    hcd->device_id = pci_dev->device_id;
    hcd->mmio_base = mmio_base;
    hcd->irq = pci_dev->interrupt_line;
    hcd->ops = &ehci_hcd_ops;
    hcd->private = ctx;
    
    ctx->hcd = hcd;
    ctx->irq = hcd->irq;
    g_ehci = ctx;
    
    // Setup IRQ
    if (ehci_setup_irq(ctx, pci_dev) != 0) {
        terminal_warn_printf("[EHCI] IRQ setup failed, using polling\n");
    }
    
    // Register with USB core
    if (usb_hcd_register(hcd) != 0) {
        free(hcd);
        free(ctx);
        return -1;
    }
    
    return 0;
}

// ==================== PCI PROBE ====================

void ehci_probe_all(void) {
    extern pci_device_t *pci_devices;
    pci_device_t *pci_dev = pci_devices;
    
    while (pci_dev) {
        if (pci_dev->class_code == EHCI_CLASS_CODE &&
            pci_dev->subclass == EHCI_SUBCLASS &&
            pci_dev->prog_if == EHCI_PROG_IF) {
            
            ehci_init(pci_dev);
        }
        pci_dev = pci_dev->next;
    }
}