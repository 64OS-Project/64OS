#include <net/rtl8139.h>
#include <net/ethernet.h>
#include <asm/io.h>
#include <asm/cpu.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <ioapic.h>
#include <apic.h>
#include <kernel/timer.h>
#include <kernel/driver.h>
#include <idt.h>

/*
 * Global pointer to driver (for IRQ)
 */
static driver_t *g_rtl8139_drv = NULL;
static rtl8139_private_t *g_rtl8139_priv = NULL;

/*
 * =============================================================================== Register operations (I/O mapped) ================================================================================
 */

static inline u8 rtl_read8(rtl8139_private_t *priv, u16 reg) {
    return inb(priv->iobase + reg);
}

static inline u16 rtl_read16(rtl8139_private_t *priv, u16 reg) {
    return inw(priv->iobase + reg);
}

static inline u32 rtl_read32(rtl8139_private_t *priv, u16 reg) {
    return inl(priv->iobase + reg);
}

static inline void rtl_write8(rtl8139_private_t *priv, u16 reg, u8 val) {
    outb(priv->iobase + reg, val);
}

static inline void rtl_write16(rtl8139_private_t *priv, u16 reg, u16 val) {
    outw(priv->iobase + reg, val);
}

static inline void rtl_write32(rtl8139_private_t *priv, u16 reg, u32 val) {
    outl(priv->iobase + reg, val);
}

/*
 * ============================================================================== Reading MAC Address ===============================================================================
 */

static void rtl8139_read_mac(rtl8139_private_t *priv, u8 *mac) {
    /*
 * Reading MAC from IDR registers
 */
    for (int i = 0; i < 6; i++) {
        mac[i] = rtl_read8(priv, RTL_IDR0 + i);
    }
    
    /*
 * Checking the validity
 */
    bool invalid = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0 && mac[i] != 0xFF) {
            invalid = false;
            break;
        }
    }
    
    if (invalid) {
        terminal_warn_printf("[RTL8139] Invalid MAC from IDR\n");
        /*
 * If invalid, try reading from EEPROM
 */
        rtl_write8(priv, RTL_9346CR, RTL_9346CR_EEM1);
        timer_mdelay(1);
        
        for (int i = 0; i < 3; i++) {
            rtl_write8(priv, RTL_9346CR, RTL_9346CR_EEM0);
            rtl_write32(priv, RTL_IDR0 + i * 2, 0);
            
            u16 cmd = 0x30 | (i << 1);
            rtl_write16(priv, RTL_IDR0, cmd);
            timer_mdelay(1);
            
            u16 val = rtl_read16(priv, RTL_IDR0);
            mac[i*2] = val & 0xFF;
            mac[i*2+1] = (val >> 8) & 0xFF;
        }
        
        rtl_write8(priv, RTL_9346CR, 0x00);
    }
    
    char mac_str[18];
    ethernet_mac_ntop(mac, mac_str, 18);
    terminal_printf("[RTL8139] MAC address: %s\n", mac_str);
}

/*
 * ================================================================================= Initializing buffers ======================================================================================
 */

static int rtl8139_init_buffers(rtl8139_private_t *priv) {
    extern pmm_t pmm;
    
    /*
 * Allocate RX buffer (8KB = 2 pages)
 */
    priv->rx_buffer = (u8*)pmm_alloc_page(&pmm);
    if (!priv->rx_buffer) {
        terminal_error_printf("[RTL8139] Failed to allocate RX buffer page 1\n");
        return -1;
    }
    
    void *rx_page2 = pmm_alloc_page(&pmm);
    if (!rx_page2) {
        terminal_error_printf("[RTL8139] Failed to allocate RX buffer page 2\n");
        pmm_free_page(&pmm, priv->rx_buffer);
        return -1;
    }
    
    priv->rx_buffer_phys = (uptr)priv->rx_buffer;
    
    terminal_printf("[RTL8139] RX buffer at phys 0x%llx, virt %p\n",
                   priv->rx_buffer_phys, priv->rx_buffer);
    
    /*
 * Allocating TX buffers (4 x 1536 bytes)
 */
    for (int i = 0; i < RTL_NUM_TX_DESC; i++) {
        priv->tx_buffers[i] = (u8*)pmm_alloc_page(&pmm);
        if (!priv->tx_buffers[i]) {
            terminal_error_printf("[RTL8139] Failed to allocate TX buffer %d\n", i);
            return -1;
        }
        priv->tx_buffers_phys[i] = (uptr)priv->tx_buffers[i];
        
        /*
 * Resetting the buffer to zero
 */
        memset(priv->tx_buffers[i], 0, PAGE_SIZE);
        
        terminal_printf("[RTL8139] TX buffer %d at phys 0x%llx\n",
                       i, priv->tx_buffers_phys[i]);
    }
    
    priv->tx_cur = 0;
    priv->tx_dirty = 0;
    
    return 0;
}

/*
 * ============================================================================= Resetting and setting up the controller ================================================================================
 */

static void rtl8139_reset(rtl8139_private_t *priv) {
    /*
 * Resetting the controller
 */
    rtl_write8(priv, RTL_CR, RTL_CR_RST);
    
    /*
 * Waiting for the reset to complete
 */
    int timeout = 1000;
    while (timeout--) {
        if (!(rtl_read8(priv, RTL_CR) & RTL_CR_RST)) {
            break;
        }
        timer_mdelay(1);
    }
    
    if (timeout == 0) {
        terminal_error_printf("[RTL8139] Reset timeout\n");
    }
    
    /*
 * Stopping TX/RX
 */
    rtl_write8(priv, RTL_CR, 0x00);
    
    /*
 * Setting up RX buffer
 */
    rtl_write32(priv, RTL_RBSTART, priv->rx_buffer_phys);
    
    /*
 * Setting up RX configuration (accept everything)
 */
    u32 rcr = RTL_RCR_WRAP | RTL_RCR_ACCEPT_ALL;
    rtl_write32(priv, RTL_RCR, rcr);
    
    /*
 * Setting up TX configuration
 */
    u32 tcr = RTL_TCR_AUTO | RTL_TCR_CRC;
    rtl_write32(priv, RTL_TCR, tcr);
    
    /*
 * Clearing interrupts
 */
    rtl_write16(priv, RTL_ISR, 0xFFFF);
    
    /*
 * Setting up the interrupt mask
 */
    rtl_write16(priv, RTL_IMR, RTL_INTR_MASK);
    
    /*
 * Turn on reception and all transmitters
 */
    rtl_write8(priv, RTL_CR, RTL_CR_RE | RTL_CR_TE0 | RTL_CR_TE1 | RTL_CR_TE2 | RTL_CR_TE3);
    
    terminal_printf("[RTL8139] Controller initialized (RCR=0x%x, TCR=0x%x)\n", rcr, tcr);
}

/*
 * ============================================================================== Sending a packet (xmit callback) ================================================================================
 */

int rtl8139_xmit(net_buf_t *buf, net_device_t *dev) {
    rtl8139_private_t *priv = (rtl8139_private_t*)dev->priv;
    if (!priv || !priv->link_up) return -1;
    
    u32 tx_idx = priv->tx_cur % RTL_NUM_TX_DESC;
    
    /*
 * Checking if the handle is free
 */
    u32 tx_status = rtl_read32(priv, RTL_TX_STATUS0 + tx_idx * 4);
    if (tx_status & 0x80000000) {
        /*
 * Descriptor busy - TODO: queue or wait
 */
        terminal_warn_printf("[RTL8139] TX descriptor %d busy\n", tx_idx);
        return -1;
    }
    
    /*
 * Copying data to TX buffer
 */
    u32 len = buf->len;
    if (len > RTL_TX_BUF_SIZE) len = RTL_TX_BUF_SIZE;
    
    memcpy(priv->tx_buffers[tx_idx], buf->data, len);
    
    /*
 * Set the buffer address
 */
    rtl_write32(priv, RTL_TX_ADDR0 + tx_idx * 4, priv->tx_buffers_phys[tx_idx]);
    
    /*
 * We start the transfer (lower 2 bytes - size, bit 31 - OWN)
 */
    rtl_write32(priv, RTL_TX_STATUS0 + tx_idx * 4, len);
    
    priv->tx_cur++;
    
    return 0;
}

/*
 * ============================================================================== Receiving the package ===================================================================================
 */

static void rtl8139_receive(rtl8139_private_t *priv) {
    u16 cur_cbr = rtl_read16(priv, RTL_CBR);
    u16 cur_capr = rtl_read16(priv, RTL_CAPR);
    
    u16 rx_offset = cur_capr;
    u8 *rx_buffer = priv->rx_buffer;
    
    while (rx_offset < RTL_RX_BUF_SIZE) {
        /*
 * Reading the packet header
 */
        u32 rx_status = *(u32*)(rx_buffer + rx_offset);
        u16 rx_len = rx_status >> 16;
        u8 rx_valid = (rx_status >> 8) & 0xFF;
        
        if (rx_len == 0 || rx_len > 2000) break;
        if (rx_valid != 1) {
            /*
 * The package is invalid
 */
            rx_offset += rx_len + 4;
            continue;
        }
        
        /*
 * Skip header (4 bytes)
 */
        u8 *packet = rx_buffer + rx_offset + 4;
        u32 packet_len = rx_len - 4;
        
        if (packet_len > 0 && packet_len <= NET_MTU) {
            /*
 * Create net_buf and pass it to the network stack
 */
            net_buf_t *buf = net_alloc_buf(packet_len);
            if (buf) {
                memcpy(buf->data, packet, packet_len);
                buf->len = packet_len;
                
                net_rx(buf, priv->net_dev);
            }
        }
        
        /*
 * Updating CAPR
 */
        rx_offset = (rx_offset + rx_len + 4 + 3) & ~3;
        if (rx_offset >= RTL_RX_BUF_SIZE) {
            rx_offset = 0;
        }
        rtl_write16(priv, RTL_CAPR, rx_offset - 0x10);
    }
}

/*
 * =============================================================================== Interrupt handler ====================================================================================
 */

void rtl8139_irq_handler(void *context) {
    rtl8139_private_t *priv = (rtl8139_private_t*)context;
    if (!priv) return;
    
    u16 isr = rtl_read16(priv, RTL_ISR);
    
    /*
 * Clearing processed interrupts
 */
    rtl_write16(priv, RTL_ISR, isr);
    
    if (isr & RTL_ISR_RX_OK) {
        /*
 * Package accepted
 */
        rtl8139_receive(priv);
    }
    
    if (isr & (RTL_ISR_TOK | RTL_ISR_TX_OK)) {
        /*
 * Transfer completed - release descriptors
 */
        while (priv->tx_dirty < priv->tx_cur) {
            u32 tx_idx = priv->tx_dirty % RTL_NUM_TX_DESC;
            u32 tx_status = rtl_read32(priv, RTL_TX_STATUS0 + tx_idx * 4);
            
            if (tx_status & 0x80000000) break;  /*
 * Not finished yet
 */
            
            priv->tx_dirty++;
        }
    }
    
    if (isr & (RTL_ISR_RX_ERR | RTL_ISR_RX_OVW | RTL_ISR_FOVW)) {
        terminal_warn_printf("[RTL8139] Receive error: ISR=0x%x\n", isr);
        /*
 * Reset error
 */
        rtl_write16(priv, RTL_ISR, isr);
    }
    
    if (isr & RTL_ISR_TER) {
        terminal_warn_printf("[RTL8139] Transmit error\n");
    }
    
    /*
 * Sending EOI
 */
    ioapic_eoi(priv->gsi);
}

/*
 * ================================================================================================ IRQ Settings =================================================================================
 */

static int rtl8139_setup_irq(rtl8139_private_t *priv) {
    priv->irq = priv->pci_dev->interrupt_line;
    
    if (!ioapic_get_override(priv->irq, &priv->gsi, NULL)) {
        priv->gsi = priv->irq;
    }
    
    /*
 * Interrupt vector (can be dynamic)
 */
    priv->vector = 40 + (priv->gsi % 16);
    
    if (!ioapic_redirect_irq(priv->gsi, priv->vector, apic_get_id(),
                              IOAPIC_FLAG_EDGE_TRIGGERED | IOAPIC_FLAG_ACTIVE_HIGH)) {
        terminal_error_printf("[RTL8139] Failed to redirect IRQ %d (GSI %d)\n",
                             priv->irq, priv->gsi);
        return -1;
    }
    
    ioapic_unmask_irq(priv->gsi);
    
    terminal_printf("[RTL8139] IRQ %d (GSI %d) -> vector %d\n",
                   priv->irq, priv->gsi, priv->vector);
    
    return 0;
}

/*
 * ============================================================================== Driver initialization ================================================================================
 */

int rtl8139_init(rtl8139_private_t *priv) {
    if (!priv) return -1;
    
    terminal_printf("[RTL8139] Initializing...\n");
    
    /*
 * Turn on the PCI device
 */
    pci_enable(priv->pci_dev);
    pci_enable_busmaster(priv->pci_dev);
    
    /*
 * We get I/O base (BAR0)
 */
    priv->iobase = priv->pci_dev->bars[0] & ~0x3;
    if (!priv->iobase) {
        terminal_error_printf("[RTL8139] No I/O base\n");
        return -1;
    }
    
    terminal_printf("[RTL8139] I/O base: 0x%x\n", priv->iobase);
    
    /*
 * Reading the MAC address
 */
    u8 mac[6];
    rtl8139_read_mac(priv, mac);
    
    /*
 * Allocating buffers
 */
    if (rtl8139_init_buffers(priv) != 0) {
        return -1;
    }
    
    /*
 * Resetting and setting up the controller
 */
    rtl8139_reset(priv);
    
    /*
 * IRQ setting
 */
    if (rtl8139_setup_irq(priv) != 0) {
        return -1;
    }
    
    /*
 * Registering a network device
 */
    priv->net_dev = (net_device_t*)malloc(sizeof(net_device_t));
    if (!priv->net_dev) {
        terminal_error_printf("[RTL8139] Failed to allocate net_device\n");
        return -1;
    }
    
    memset(priv->net_dev, 0, sizeof(net_device_t));
    snprintf(priv->net_dev->name, sizeof(priv->net_dev->name), "eth0");
    memcpy(priv->net_dev->mac_addr, mac, 6);
    priv->net_dev->mtu = NET_MTU;
    priv->net_dev->priv = priv;
    priv->net_dev->xmit = rtl8139_xmit;
    priv->net_dev->up = true;
    priv->net_dev->running = true;
    priv->net_dev->type = NET_TYPE_ETHERNET;
    
    net_register_device(priv->net_dev);
    
    priv->link_up = true;
    
    terminal_success_printf("[RTL8139] Initialized successfully\n");
    return 0;
}

/*
 * ============================================================================= Driver subsystem functions ============================================================================
 */

static int rtl8139_probe(driver_t *drv) {
    (void)drv;
    
    /*
 * Looking for RTL8139 by PCI ID
 */
    pci_device_t *pci_dev = pci_find(0x10EC, 0x8139);
    if (!pci_dev) {
        /*
 * Trying other IDs
 */
        pci_dev = pci_find(0x1186, 0x1300);  /*
 * D-Link DFE-538TX
 */
    }
    if (!pci_dev) {
        pci_dev = pci_find(0x1113, 0x1211);  /*
 * Accton
 */
    }
    if (!pci_dev) {
        return 1;  /*
 * Device not found
 */
    }
    
    /*
 * We save the PCI device as private data
 */
    rtl8139_private_t *priv = (rtl8139_private_t*)driver_get_private(drv, sizeof(rtl8139_private_t));
    if (!priv) return -1;
    
    memset(priv, 0, sizeof(rtl8139_private_t));
    priv->pci_dev = pci_dev;
    
    return 0;  /*
 * Device found
 */
}

static int rtl8139_init_driver(driver_t *drv) {
    rtl8139_private_t *priv = (rtl8139_private_t*)drv->priv;
    if (!priv) return -1;
    
    g_rtl8139_priv = priv;
    g_rtl8139_drv = drv;
    
    return rtl8139_init(priv);
}

static void rtl8139_remove(driver_t *drv) {
    rtl8139_private_t *priv = (rtl8139_private_t*)drv->priv;
    if (!priv) return;
    
    extern pmm_t pmm;
    
    /*
 * Disable interrupts
 */
    rtl_write16(priv, RTL_IMR, 0);
    ioapic_mask_irq(priv->gsi);
    
    /*
 * Stopping the controller
 */
    rtl_write8(priv, RTL_CR, 0);
    
    /*
 * Freeing up buffers
 */
    if (priv->rx_buffer) {
        pmm_free_page(&pmm, priv->rx_buffer);
    }
    
    for (int i = 0; i < RTL_NUM_TX_DESC; i++) {
        if (priv->tx_buffers[i]) {
            pmm_free_page(&pmm, priv->tx_buffers[i]);
        }
    }
    
    /*
 * Removing a network device
 */
    if (priv->net_dev) {
        net_unregister_device(priv->net_dev);
        free(priv->net_dev);
    }
    
    free(priv);
    drv->priv = NULL;
    
    terminal_printf("[RTL8139] Removed\n");
}

/*
 * ============================================================================= Driver registration ============================================================================
 */

driver_t g_rtl8139_driver = {
    .name = "rtl8139",
    .desc = "Realtek RTL8139 Fast Ethernet Driver",
    .critical_level = DRIVER_CRITICAL_3,
    .probe = rtl8139_probe,
    .init = rtl8139_init_driver,
    .remove = rtl8139_remove,
    .priv = NULL,
};
