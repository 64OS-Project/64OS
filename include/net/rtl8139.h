#ifndef NET_RTL8139_H
#define NET_RTL8139_H

#include <net/net.h>
#include <pci.h>
#include <kernel/driver.h>

/*
 * ============================================================================== RTL8139 registers (I/O mapped) ================================================================================
 */

/*
 * Configuration registers
 */
#define RTL_IDR0           0x00    /*
 * MAC address (6 bytes)
 */
#define RTL_MAR0           0x08    /*
 * Multicast filter (8 bytes)
 */
#define RTL_TX_STATUS0     0x10    /*
 * TX status (4 registers, 0x10-0x1C)
 */
#define RTL_TX_ADDR0       0x20    /*
 * TX address (4 registers, 0x20-0x2C)
 */

/*
 * Receive buffer
 */
#define RTL_RBSTART        0x30    /*
 * Receive buffer start address (32-bit)
 */
#define RTL_CR             0x37    /*
 * Command register (8-bit)
 */
#define RTL_CAPR           0x38    /*
 * Current address of packet read (16-bit)
 */
#define RTL_CBR            0x3A    /*
 * Current buffer address (16-bit)
 */
#define RTL_IMR            0x3C    /*
 * Interrupt mask register (16-bit)
 */
#define RTL_ISR            0x3E    /*
 * Interrupt status register (16-bit)
 */
#define RTL_TCR            0x40    /*
 * Transmit configuration (32-bit)
 */
#define RTL_RCR            0x44    /*
 * Receive configuration (32-bit)
 */
#define RTL_MPC            0x4C    /*
 * Missed packet counter (16-bit)
 */
#define RTL_9346CR         0x50    /*
 * 93C46 command register (8-bit)
 */
#define RTL_CONFIG0        0x51    /*
 * Configuration register 0 (8-bit)
 */
#define RTL_CONFIG1        0x52    /*
 * Configuration register 1 (8-bit)
 */
#define RTL_TIMER          0x54    /*
 * Timer interrupt (32-bit)
 */
#define RTL_MSK            0x58    /*
 * Timer mask (8-bit)
 */

/*
 * Command register bits (CR)
 */
#define RTL_CR_BUFE        0x01    /*
 * Buffer empty
 */
#define RTL_CR_TE0         0x04    /*
 * Transmit enable for TX0
 */
#define RTL_CR_TE1         0x20    /*
 * Transmit enable for TX1
 */
#define RTL_CR_TE2         0x40    /*
 * Transmit enable for TX2
 */
#define RTL_CR_TE3         0x80    /*
 * Transmit enable for TX3
 */
#define RTL_CR_RE          0x08    /*
 * Receiver enable
 */
#define RTL_CR_RST         0x10    /*
 * Reset
 */

/*
 * Interrupt status/mask bits (ISR/IMR)
 */
#define RTL_ISR_PCIERR     0x8000  /*
 * PCI error
 */
#define RTL_ISR_PWR        0x4000  /*
 * Power change
 */
#define RTL_ISR_FOVW       0x2000  /*
 * Receive FIFO overflow
 */
#define RTL_ISR_RDU        0x1000  /*
 * Receive descriptor unavailable
 */
#define RTL_ISR_TOK        0x0800  /*
 * Transmit OK
 */
#define RTL_ISR_TER        0x0400  /*
 * Transmit error
 */
#define RTL_ISR_RX_OK      0x0001  /*
 * Receive OK
 */
#define RTL_ISR_RX_ERR     0x0002  /*
 * Receive error
 */
#define RTL_ISR_TX_OK      0x0004  /*
 * Transmit OK (legacy)
 */
#define RTL_ISR_TX_ERR     0x0008  /*
 * Transmit error (legacy)
 */
#define RTL_ISR_RX_OVW     0x0010  /*
 * Receive overflow
 */

/*
 * Interrupts we care about
 */
#define RTL_INTR_MASK      (RTL_ISR_RX_OK | RTL_ISR_RX_ERR | \
                            RTL_ISR_TOK | RTL_ISR_TER | \
                            RTL_ISR_RX_OVW | RTL_ISR_FOVW)

/*
 * Receive configuration bits (RCR)
 */
#define RTL_RCR_AAP        0x00000001  /*
 * Accept all packets (promiscuous)
 */
#define RTL_RCR_APM        0x00000002  /*
 * Accept multicast
 */
#define RTL_RCR_AM         0x00000004  /*
 * Accept multicast (promiscuous)
 */
#define RTL_RCR_AB         0x00000008  /*
 * Accept broadcast
 */
#define RTL_RCR_WRAP       0x00000080  /*
 * Wrap at end of buffer
 */
#define RTL_RCR_ERUN       0x00000800  /*
 * Early run (reserved)
 */
#define RTL_RCR_ACCEPT_ALL (RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB)

/*
 * Transmit configuration bits (TCR)
 */
#define RTL_TCR_IFG        0x00000300  /*
 * Interframe gap
 */
#define RTL_TCR_CLR        0x00008000  /*
 * Collision lock release
 */
#define RTL_TCR_AUTO       0x00004000  /*
 * Auto-padding
 */
#define RTL_TCR_LB         0x00000C00  /*
 * Loopback test
 */
#define RTL_TCR_CRC        0x00002000  /*
 * Append CRC
 */

/*
 * 9346CR bits
 */
#define RTL_9346CR_EEM0    0x40        /*
 * EEPROM mode 0
 */
#define RTL_9346CR_EEM1    0x80        /*
 * EEPROM mode 1
 */

/*
 * Transmit buffer sizes
 */
#define RTL_TX_BUF_SIZE    1536        /*
 * Maximum frame size
 */
#define RTL_NUM_TX_DESC    4           /*
 * 4 TX descriptors
 */
#define RTL_RX_BUF_SIZE    8192        /*
 * 8KB receive buffer
 */

/*
 * ============================================================================== RTL8139 driver =================================================================================
 */

typedef struct rtl8139_private {
    pci_device_t *pci_dev;
    
    /*
 * I/O port
 */
    u16 iobase;
    
    /*
 * IRQ
 */
    u8 irq;
    u32 gsi;
    u8 vector;
    
    /*
 * Buffers
 */
    u8 *rx_buffer;          /*
 * Ring receive buffer
 */
    uptr rx_buffer_phys;
    
    u8 *tx_buffers[RTL_NUM_TX_DESC];
    uptr tx_buffers_phys[RTL_NUM_TX_DESC];
    u32 tx_cur;
    u32 tx_dirty;
    
    /*
 * State
 */
    bool link_up;
    
    /*
 * Network device
 */
    net_device_t *net_dev;
} rtl8139_private_t;

/*
 * ============================================================================== Driver functions ================================================================================
 */

/*
 * Driver structure
 */
extern driver_t g_rtl8139_driver;

/*
 * Initialization
 */
int rtl8139_init(rtl8139_private_t *priv);

/*
 * Interrupt handler
 */
void rtl8139_irq_handler(void *context);

/*
 * Sending a package
 */
int rtl8139_xmit(net_buf_t *buf, net_device_t *dev);

#endif /*
 * NET_RTL8139_H
 */
