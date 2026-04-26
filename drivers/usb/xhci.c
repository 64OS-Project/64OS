// drivers/usb/xhci.c
#include <usb/xhci.h>
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

static xhci_ctx_t *g_xhci = NULL;
extern pmm_t pmm;

// ==================== MMIO ACCESS ====================

static inline u32 xhci_read32(volatile void *base, u32 reg) {
    return mmio_read32((volatile void*)((uptr)base + reg));
}

static inline void xhci_write32(volatile void *base, u32 reg, u32 val) {
    mmio_write32((volatile void*)((uptr)base + reg), val);
}

static inline void xhci_write64(volatile void *base, u32 reg, u64 val) {
    xhci_write32(base, reg, val & 0xFFFFFFFF);
    xhci_write32(base, reg + 4, (val >> 32) & 0xFFFFFFFF);
}

// ==================== RING MANAGEMENT ====================

static int xhci_alloc_ring(xhci_ctx_t *ctx, xhci_ring_t *ring, u32 size) {
    u32 pages = (size * sizeof(xhci_trb_t) + PAGE_SIZE - 1) / PAGE_SIZE;
    
    ring->trbs = (xhci_trb_t*)pmm_alloc_range(&pmm, pages);
    if (!ring->trbs) return -1;
    
    ring->ring_phys = (u32)(uptr)ring->trbs;
    memset(ring->trbs, 0, pages * PAGE_SIZE);
    
    ring->enqueue = 0;
    ring->dequeue = 0;
    ring->cycle_state = 1;
    ring->size = size;
    
    // Setup link TRB for wrap-around
    if (size > 1) {
        xhci_link_trb_t *link = (xhci_link_trb_t*)&ring->trbs[size - 1];
        memset(link, 0, sizeof(xhci_link_trb_t));
        link->segment_ptr_low = ring->ring_phys;
        link->segment_ptr_high = 0;
        link->trb_type = XHCI_TRB_TYPE_LINK;
        link->cycle_bit = 1;
        link->toggle_cycle = 1;
    }
    
    return 0;
}

static void xhci_free_ring(xhci_ctx_t *ctx, xhci_ring_t*ring) {
    if (ring->trbs) {
        u32 pages = (ring->size * sizeof(xhci_trb_t) + PAGE_SIZE - 1) / PAGE_SIZE;
        pmm_free_range(&pmm, (void*)ring->trbs, pages);
        ring->trbs = NULL;
    }
}

static void xhci_ring_enqueue(xhci_ctx_t *ctx, xhci_ring_t*ring, xhci_trb_t *trb) {
    u32 idx = ring->enqueue;
    
    memcpy(&ring->trbs[idx], trb, sizeof(xhci_trb_t));
    
    if (ring->cycle_state) {
        ring->trbs[idx].control |= XHCI_TRB_CYCLE_BIT;
    } else {
        ring->trbs[idx].control &= ~XHCI_TRB_CYCLE_BIT;
    }
    
    ring->enqueue++;
    if (ring->enqueue >= ring->size - 1) {
        ring->enqueue = 0;
        ring->cycle_state ^= 1;
    }
}

static void xhci_ring_enqueue_command(xhci_ctx_t *ctx, void *cmd_trb) {
    xhci_ring_enqueue(ctx, &ctx->cmd_ring, (xhci_trb_t*)cmd_trb);
    
    // Ring doorbell 0
    xhci_write32(ctx->doorbell, 0, 0);
}

// ==================== EVENT RING PROCESSING ====================

static void xhci_scan_ports(xhci_ctx_t *ctx);

static void xhci_process_events(xhci_ctx_t *ctx) {
    while (1) {
        xhci_trb_t *trb = &ctx->evt_ring.trbs[ctx->evt_ring.dequeue];
        u32 cycle = trb->control & XHCI_TRB_CYCLE_BIT;
        
        if (cycle != ctx->evt_ring.cycle_state) {
            break;
        }
        
        u32 type = (trb->control >> 10) & 0x3F;
        u32 code = (trb->status >> 24) & 0xFF;
        
        switch (type) {
            case XHCI_TRB_TYPE_TRANSFER_EVENT: {
                xhci_transfer_event_trb_t *ev = (xhci_transfer_event_trb_t*)trb;
                terminal_debug_printf("[xHCI] Xfer event: slot=%d, ep=%d, len=%d, code=%d\n",
                                     ev->slot_id, ev->endpoint_id, ev->transfer_length, code);
                break;
            }
            case XHCI_TRB_TYPE_CMD_COMPLETE_EVENT: {
                xhci_cmd_completion_event_trb_t *ev = (xhci_cmd_completion_event_trb_t*)trb;
                terminal_debug_printf("[xHCI] Cmd complete: slot=%d, code=%d\n",
                                     ev->slot_id, code);
                break;
            }
            case XHCI_TRB_TYPE_PORT_STATUS_EVENT: {
                xhci_port_status_event_trb_t *ev = (xhci_port_status_event_trb_t*)trb;
                terminal_printf("[xHCI] Port status change: port=%d\n", ev->port_id);
                // Re-scan ports
                xhci_scan_ports(ctx);
                break;
            }
        }
        
        ctx->evt_ring.dequeue++;
        if (ctx->evt_ring.dequeue >= ctx->evt_ring.size) {
            ctx->evt_ring.dequeue = 0;
            ctx->evt_ring.cycle_state ^= 1;
        }
        
        // Update ERDP
        u64 erdp = (u64)(uptr)&ctx->evt_ring.trbs[ctx->evt_ring.dequeue];
        xhci_write64(ctx->op_regs, 0x38, erdp | 1);  // ERDP register
    }
}

// ==================== COMMANDS WITH COMPLETION ====================

static int xhci_send_command(xhci_ctx_t *ctx, void *cmd_trb, u32 slot_id, u32 timeout_ms) {
    // Save current enqueue position to track completion
    u32 cmd_enqueue = ctx->cmd_ring.enqueue;
    
    xhci_ring_enqueue_command(ctx, cmd_trb);
    
    // Wait for command completion
    u64 start = timer_apic_ticks();
    while (timer_apic_ticks() - start < timeout_ms) {
        // Process events
        xhci_process_events(ctx);
        
        // Check if our command completed by looking at command ring dequeue
        // This is simplified - real implementation should track completion via event data
        if (ctx->cmd_ring.dequeue != cmd_enqueue) {
            return 0;
        }
        
        timer_udelay(100);
    }
    
    terminal_error_printf("[xHCI] Command timeout\n");
    return -1;
}

static int xhci_enable_slot(xhci_ctx_t *ctx, u8 *slot_id) {
    xhci_enable_slot_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.trb_type = XHCI_TRB_TYPE_ENABLE_SLOT;
    
    if (xhci_send_command(ctx, &cmd, 0, 1000) != 0) {
        return -1;
    }
    
    // In real implementation, slot_id comes from command completion event
    *slot_id = 1;
    return 0;
}

static int xhci_address_device(xhci_ctx_t *ctx, u8 slot_id, u64 input_ctx_ptr) {
    xhci_address_device_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.input_ctx_ptr_low = input_ctx_ptr & 0xFFFFFFFF;
    cmd.input_ctx_ptr_high = (input_ctx_ptr >> 32) & 0xFFFFFFFF;
    cmd.trb_type = XHCI_TRB_TYPE_ADDRESS_DEVICE;
    cmd.slot_id = slot_id;
    cmd.bsr = 0;
    
    return xhci_send_command(ctx, &cmd, slot_id, 1000);
}

static int xhci_configure_ep(xhci_ctx_t *ctx, u8 slot_id, u64 input_ctx_ptr) {
    xhci_configure_ep_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.input_ctx_ptr_low = input_ctx_ptr & 0xFFFFFFFF;
    cmd.input_ctx_ptr_high = (input_ctx_ptr >> 32) & 0xFFFFFFFF;
    cmd.trb_type = XHCI_TRB_TYPE_CONFIGURE_EP;
    cmd.slot_id = slot_id;
    cmd.deconfigure = 0;
    
    return xhci_send_command(ctx, &cmd, slot_id, 1000);
}

// ==================== DEVICE CONTEXT SETUP ====================

static void xhci_setup_input_context(xhci_ctx_t *ctx, u8 slot_id, u64 input_ctx_ptr) {
    xhci_input_context_t *input = (xhci_input_context_t*)(uptr)input_ctx_ptr;
    memset(input, 0, PAGE_SIZE);
    
    // Add slot and ep0 contexts
    input->add_flags = (1 << 0) | (1 << 1);
    
    // Slot context
    input->slot.route_string = 0;
    input->slot.speed = 2;  // High speed (default)
    input->slot.hub = 0;
    input->slot.context_entries = 1;
    input->slot.root_hub_port_num = 1;
    input->slot.device_address = 0;
    input->slot.slot_state = 0;
    
    // Endpoint 0 context
    input->ep[0].ep_type = 4;  // Control endpoint
    input->ep[0].max_packet_size = 8;
    input->ep[0].max_burst_size = 0;
    input->ep[0].interval = 0;
    input->ep[0].error_count = 3;
    input->ep[0].tr_dequeue_pointer_low = 0;
    input->ep[0].tr_dequeue_pointer_high = 0;
    input->ep[0].dequeue_cycle = 1;
}

// ==================== PORT MANAGEMENT ====================

static void xhci_scan_ports(xhci_ctx_t *ctx) {
    for (u32 i = 0; i < ctx->max_ports; i++) {
        volatile u32 *portsc = (volatile u32*)((uptr)ctx->port_regs + i * XHCI_PORTSC_SIZE);
        u32 status = xhci_read32(portsc, 0);
        
        bool connected = (status & XHCI_PORTSC_CCS) != 0;
        
        if (connected && !ctx->ports[i].connected) {
            terminal_printf("[xHCI] Port %d: device connected\n", i);
            ctx->ports[i].connected = true;
            
            // Reset port
            xhci_write32(portsc, 0, status | XHCI_PORTSC_PR);
            timer_mdelay(10);
            xhci_write32(portsc, 0, status & ~XHCI_PORTSC_PR);
            timer_mdelay(10);
            
            // Enable slot
            u8 slot_id;
            if (xhci_enable_slot(ctx, &slot_id) == 0) {
                terminal_printf("[xHCI] Slot %d enabled\n", slot_id);
                
                // Allocate device context
                void *dev_ctx_phys = pmm_alloc_page(&pmm);
                if (dev_ctx_phys) {
                    ctx->dcbaa[slot_id] = (u64)(uptr)dev_ctx_phys;
                    memset((void*)(uptr)dev_ctx_phys, 0, PAGE_SIZE);
                    
                    // Allocate input context
                    void *input_ctx_phys = pmm_alloc_page(&pmm);
                    if (input_ctx_phys) {
                        xhci_setup_input_context(ctx, slot_id, (u64)(uptr)input_ctx_phys);
                        
                        // Address device
                        if (xhci_address_device(ctx, slot_id, (u64)(uptr)input_ctx_phys) == 0) {
                            terminal_printf("[xHCI] Device addressed\n");
                            
                            // Create USB device
                            usb_device_t *dev = usb_device_alloc(ctx->hcd);
                            if (dev) {
                                dev->address = slot_id;
                                dev->speed = 2;
                                dev->max_packet_size0 = 8;
                                dev->hcd = ctx->hcd;
                                
                                // Note: enumeration will set proper max_packet_size
                                if (usb_enumeration(dev) == 0) {
                                    usb_device_add(ctx->hcd, dev);
                                    ctx->ports[i].slot_id = slot_id;
                                } else {
                                    usb_device_free(dev);
                                }
                            }
                        }
                        
                        pmm_free_page(&pmm, input_ctx_phys);
                    }
                }
            }
        } else if (!connected && ctx->ports[i].connected) {
            terminal_printf("[xHCI] Port %d: device disconnected\n", i);
            ctx->ports[i].connected = false;
            ctx->ports[i].slot_id = 0;
        }
        
        // Clear change bits
        if (status & XHCI_PORTSC_CSC) {
            xhci_write32(portsc, 0, status);
        }
    }
}

// ==================== INTERRUPT HANDLER ====================

void xhci_irq_handler(usb_hcd_t *hcd) {
    xhci_ctx_t *ctx = (xhci_ctx_t*)hcd->private;
    if (!ctx) return;
    
    u32 status = xhci_read32(ctx->op_regs, XHCI_USBSTS);
    
    // Clear interrupts
    xhci_write32(ctx->op_regs, XHCI_USBSTS, status);
    
    if (status & XHCI_STS_EINT) {
        xhci_process_events(ctx);
    }
    
    if (status & XHCI_STS_PCD) {
        xhci_scan_ports(ctx);
    }
    
    if (status & XHCI_STS_HSE) {
        terminal_error_printf("[xHCI] Host system error\n");
    }
    
    // Send EOI
    ioapic_eoi(ctx->gsi);
}

static int xhci_setup_irq(xhci_ctx_t *ctx, pci_device_t *pci_dev) {
    ctx->irq = pci_dev->interrupt_line;
    
    if (!ioapic_get_override(ctx->irq, &ctx->gsi, NULL)) {
        ctx->gsi = ctx->irq;
    }
    
    ctx->vector = 48 + (ctx->gsi % 16);
    
    if (!ioapic_redirect_irq(ctx->gsi, ctx->vector, apic_get_id(),
                              IOAPIC_FLAG_LEVEL_TRIGGERED | IOAPIC_FLAG_ACTIVE_HIGH)) {
        terminal_error_printf("[xHCI] Failed to redirect IRQ\n");
        return -1;
    }
    
    idt_set_gate(ctx->vector, (void*)xhci_irq_handler, KERNEL_CODE_SEL, IDT_GATE_INT, 0);
    ioapic_unmask_irq(ctx->gsi);
    
    terminal_printf("[xHCI] IRQ %d (GSI %d) -> vector %d\n", ctx->irq, ctx->gsi, ctx->vector);
    
    return 0;
}

// ==================== CONTROLLER INITIALIZATION ====================

static int xhci_controller_init(xhci_ctx_t *ctx) {
    terminal_printf("[xHCI] Initializing controller...\n");
    
    // Reset controller
    xhci_write32(ctx->op_regs, XHCI_USBCMD, XHCI_CMD_HCRST);
    timer_mdelay(10);
    
    int timeout = 10000;
    while (timeout-- && (xhci_read32(ctx->op_regs, XHCI_USBCMD) & XHCI_CMD_HCRST)) {
        timer_udelay(100);
    }
    
    if (timeout == 0) {
        terminal_error_printf("[xHCI] Reset timeout\n");
        return -1;
    }
    
    // Get parameters
    u32 hcsparams1 = xhci_read32(ctx->cap_regs, XHCI_HCSPARAMS1);
    ctx->max_slots = hcsparams1 & 0xFF;
    ctx->max_ports = (hcsparams1 >> 24) & 0xFF;
    
    terminal_printf("[xHCI] Max slots: %d, Max ports: %d\n", ctx->max_slots, ctx->max_ports);
    
    // Allocate DCBAA (Device Context Base Address Array)
    u32 dcbaa_pages = ((ctx->max_slots + 1) * 8 + PAGE_SIZE - 1) / PAGE_SIZE;
    ctx->dcbaa = (u64*)pmm_alloc_range(&pmm, dcbaa_pages);
    if (!ctx->dcbaa) return -1;
    ctx->dcbaa_phys = (u32)(uptr)ctx->dcbaa;
    memset(ctx->dcbaa, 0, dcbaa_pages * PAGE_SIZE);
    
    xhci_write64(ctx->op_regs, XHCI_DCBAAP, ctx->dcbaa_phys);
    
    // Allocate command ring
    if (xhci_alloc_ring(ctx, &ctx->cmd_ring, 64) != 0) {
        terminal_error_printf("[xHCI] Failed to allocate command ring\n");
        return -1;
    }
    
    xhci_write64(ctx->op_regs, XHCI_CRCR, (u64)ctx->cmd_ring.ring_phys | 1);
    
    // Allocate event ring
    if (xhci_alloc_ring(ctx, &ctx->evt_ring, 256) != 0) {
        terminal_error_printf("[xHCI] Failed to allocate event ring\n");
        return -1;
    }
    
    // Setup Event Ring Segment Table
    u32 erst_pages = 1;
    ctx->erst.segment = (xhci_event_ring_segment_table_entry_t*)pmm_alloc_page(&pmm);
    if (!ctx->erst.segment) return -1;
    ctx->erst.segment_phys = (u32)(uptr)ctx->erst.segment;
    ctx->erst.size = 1;
    
    ctx->erst.segment[0].ring_segment_base_address = (u64)ctx->evt_ring.ring_phys;
    ctx->erst.segment[0].ring_segment_size = ctx->evt_ring.size;
    
    // Set ERST registers
    xhci_write64(ctx->op_regs, 0x28, ctx->erst.segment_phys);  // ERSTBA
    xhci_write32(ctx->op_regs, 0x2C, ctx->erst.size);          // ERSTS
    
    // Set ERDP
    xhci_write64(ctx->op_regs, 0x38, (u64)(uptr)ctx->evt_ring.trbs | 1);  // ERDP
    
    // Setup interrupt
    xhci_write32(ctx->op_regs, XHCI_USBINTR, XHCI_INTR_EINT | XHCI_INTR_PCD);
    
    // Set number of slots
    xhci_write32(ctx->op_regs, XHCI_CONFIG, ctx->max_slots);
    
    // Run controller
    xhci_write32(ctx->op_regs, XHCI_USBCMD, XHCI_CMD_RS);
    
    // Wait for run
    timer_mdelay(10);
    
    if (xhci_read32(ctx->op_regs, XHCI_USBSTS) & XHCI_STS_HCH) {
        terminal_error_printf("[xHCI] Controller failed to start\n");
        return -1;
    }
    
    ctx->running = true;
    terminal_success_printf("[xHCI] Controller running\n");
    
    // Scan ports
    xhci_scan_ports(ctx);

    for (u32 i = 0; i < ctx->max_ports; i++) {
    	volatile u32 *portsc = (volatile u32*)((uptr)ctx->port_regs + i * XHCI_PORTSC_SIZE);
    	u32 status = xhci_read32(portsc, 0);
    	terminal_printf("[xHCI] Port %d: status=0x%08x %s\n", i, status,
                   (status & XHCI_PORTSC_CCS) ? "CONNECTED" : "");
    }
    
    return 0;
}

// ==================== HCD CALLBACKS ====================

static int xhci_hcd_init(usb_hcd_t *hcd) {
    xhci_ctx_t *ctx = (xhci_ctx_t*)hcd->private;
    if (!ctx) return -1;
    
    return xhci_controller_init(ctx);
}

static int xhci_hcd_submit_transfer(usb_hcd_t *hcd, usb_transfer_t *transfer) {
    // TODO: Implement transfer submission
    return -1;
}

static int xhci_hcd_shutdown(usb_hcd_t *hcd) {
    xhci_ctx_t *ctx = (xhci_ctx_t*)hcd->private;
    if (!ctx) return -1;
    
    xhci_write32(ctx->op_regs, XHCI_USBCMD, 0);
    ctx->running = false;
    
    xhci_free_ring(ctx, &ctx->cmd_ring);
    xhci_free_ring(ctx, &ctx->evt_ring);
    
    if (ctx->erst.segment) {
        pmm_free_page(&pmm, (void*)ctx->erst.segment);
    }
    
    if (ctx->dcbaa) {
        u32 pages = ((ctx->max_slots + 1) * 8 + PAGE_SIZE - 1) / PAGE_SIZE;
        pmm_free_range(&pmm, (void*)ctx->dcbaa, pages);
    }
    
    return 0;
}

static usb_hcd_ops_t xhci_hcd_ops = {
    .init = xhci_hcd_init,
    .shutdown = xhci_hcd_shutdown,
    .reset = NULL,
    .submit_transfer = xhci_hcd_submit_transfer,
    .cancel_transfer = NULL,
    .set_address = NULL,
    .enable_endpoint = NULL,
    .disable_endpoint = NULL,
    .irq_handler = xhci_irq_handler,
};

// ==================== PCI PROBE ====================

int xhci_init(pci_device_t *pci_dev) {
    if (!pci_dev) return -1;
    
    terminal_printf("[xHCI] Found controller at %02X:%02X.%X (%04X:%04X)\n",
                    pci_dev->bus, pci_dev->slot, pci_dev->function,
                    pci_dev->vendor_id, pci_dev->device_id);
    
    // Enable bus mastering and memory space
    pci_enable_busmaster(pci_dev);
    pci_enable(pci_dev);
    
    // Get MMIO base (BAR0)
    u32 mmio_base = pci_dev->bars[0] & ~0xF;
    if (!mmio_base) {
        terminal_error_printf("[xHCI] No MMIO BAR\n");
        return -1;
    }
    
    terminal_printf("[xHCI] MMIO base: 0x%x\n", mmio_base);
    
    // Allocate context
    xhci_ctx_t *ctx = (xhci_ctx_t*)malloc(sizeof(xhci_ctx_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(xhci_ctx_t));
    
    ctx->cap_regs = (volatile void*)(uptr)mmio_base;
    u8 caplength = mmio_read8(ctx->cap_regs);
    ctx->op_regs = (volatile void*)((uptr)ctx->cap_regs + caplength);
    
    // Get doorbell offset
    u32 doorbell_off = xhci_read32(ctx->cap_regs, XHCI_DBOFF) & ~0x3;
    ctx->doorbell = (volatile void*)((uptr)ctx->cap_regs + doorbell_off);
    
    // Get runtime offset
    u32 runtime_off = xhci_read32(ctx->cap_regs, XHCI_RTSOFF) & ~0x1F;
    ctx->runtime_regs = (volatile void*)((uptr)ctx->cap_regs + runtime_off);
    
    // Port registers are at offset 0x400 from operational registers
    ctx->port_regs = (volatile void*)((uptr)ctx->op_regs + 0x400);
    
    // Create HCD
    usb_hcd_t *hcd = (usb_hcd_t*)malloc(sizeof(usb_hcd_t));
    if (!hcd) {
        free(ctx);
        return -1;
    }
    
    memset(hcd, 0, sizeof(usb_hcd_t));
    snprintf(hcd->name, sizeof(hcd->name), "xhci");
    hcd->vendor_id = pci_dev->vendor_id;
    hcd->device_id = pci_dev->device_id;
    hcd->mmio_base = mmio_base;
    hcd->irq = pci_dev->interrupt_line;
    hcd->ops = &xhci_hcd_ops;
    hcd->private = ctx;
    
    ctx->hcd = hcd;
    g_xhci = ctx;
    
    // Setup IRQ
    if (xhci_setup_irq(ctx, pci_dev) != 0) {
        terminal_warn_printf("[xHCI] Failed to setup IRQ\n");
    }
    
    // Register with USB core
    if (usb_hcd_register(hcd) != 0) {
        free(hcd);
        free(ctx);
        return -1;
    }
    
    return 0;
}

void xhci_probe_all(void) {
    extern pci_device_t *pci_devices;
    pci_device_t *pci_dev = pci_devices;
    
    while (pci_dev) {
        if (pci_dev->class_code == XHCI_PCI_CLASS &&
            pci_dev->subclass == XHCI_PCI_SUBCLASS &&
            pci_dev->prog_if == XHCI_PCI_PROG_IF) {
            
            xhci_init(pci_dev);
        }
        pci_dev = pci_dev->next;
    }
}