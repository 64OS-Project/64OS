#ifndef EHCI_H
#define EHCI_H

#include <usb/usb.h>
#include <kernel/types.h>
#include <pci.h>

// PCI IDs
#define PCI_VENDOR_INTEL       0x8086
#define PCI_VENDOR_AMD         0x1022
#define PCI_VENDOR_NVIDIA      0x10DE
#define PCI_VENDOR_VIA         0x1106
#define PCI_VENDOR_ATI         0x1002

// EHCI PCI Class
#define EHCI_CLASS_CODE        0x0C
#define EHCI_SUBCLASS          0x03
#define EHCI_PROG_IF           0x20

// ==================== CAPABILITY REGISTERS ====================
#define EHCI_CAPLENGTH         0x00
#define EHCI_HCIVERSION        0x02
#define EHCI_HCSPARAMS         0x04
#define EHCI_HCCPARAMS         0x08

// HCSPARAMS bits
#define EHCI_HCSPARAMS_NPORTS(n)      ((n) & 0x0F)
#define EHCI_HCSPARAMS_PPC            (1 << 4)
#define EHCI_HCSPARAMS_PI             (1 << 8)

// HCCPARAMS bits
#define EHCI_HCCPARAMS_EECP           ((n) >> 8) & 0xFF)
#define EHCI_HCCPARAMS_IST            ((n) >> 4) & 0x0F)

// ==================== OPERATIONAL REGISTERS ====================
#define EHCI_USBCMD              0x00
#define EHCI_USBSTS              0x04
#define EHCI_USBINTR             0x08
#define EHCI_FRINDEX             0x0C
#define EHCI_CTRLDSSEGMENT       0x10
#define EHCI_PERIODICLISTBASE    0x14
#define EHCI_ASYNCLISTADDR       0x18
#define EHCI_CONFIGFLAG          0x40
#define EHCI_PORTSC(n)           (0x44 + (n) * 4)

// USBCMD bits
#define EHCI_CMD_RS              (1 << 0)
#define EHCI_CMD_HCRESET         (1 << 1)
#define EHCI_CMD_FLS             (3 << 2)
#define EHCI_CMD_IAAD            (1 << 6)
#define EHCI_CMD_ASE             (1 << 5)
#define EHCI_CMD_PSE             (1 << 4)
#define EHCI_CMD_LR              (1 << 7)

// USBSTS bits
#define EHCI_STS_USBINT          (1 << 0)
#define EHCI_STS_ERROR           (1 << 1)
#define EHCI_STS_PORT_CHANGE     (1 << 2)
#define EHCI_STS_HALT            (1 << 12)
#define EHCI_STS_RECL            (1 << 13)
#define EHCI_STS_ASS             (1 << 15)
#define EHCI_STS_PSS             (1 << 14)

// USBINTR bits
#define EHCI_INTR_USB            (1 << 0)
#define EHCI_INTR_ERROR          (1 << 1)
#define EHCI_INTR_PORT_CHANGE    (1 << 2)
#define EHCI_INTR_HALT           (1 << 12)

// PORTSC bits
#define EHCI_PORT_CCS            (1 << 0)
#define EHCI_PORT_CSC            (1 << 1)
#define EHCI_PORT_PE             (1 << 2)
#define EHCI_PORT_PEC            (1 << 3)
#define EHCI_PORT_RESET          (1 << 8)
#define EHCI_PORT_SUSPEND        (1 << 7)
#define EHCI_PORT_POWER          (1 << 12)
#define EHCI_PORT_OWNER          (1 << 13)
#define EHCI_PORT_SPEED_MASK     (3 << 26)
#define EHCI_PORT_SPEED_FULL     (0 << 26)
#define EHCI_PORT_SPEED_LOW      (1 << 26)
#define EHCI_PORT_SPEED_HIGH     (2 << 26)

#define EHCI_MAX_PORTS          16

// ==================== QH/qTD STRUCTURES ====================

// Queue Head
typedef struct ehci_qh {
    u32 horizontal_link;        // Next QH (0x01 = terminate)
    u32 endpoint_caps;          // Endpoint characteristics
    u32 current_qtd;            // Current qTD pointer
    u32 next_qtd;               // Next qTD pointer (dword 3)
    u32 alt_next_qtd;           // Alternate next qTD (dword 4)
    u32 token;                  // Transfer descriptor token
    u32 buffer[5];              // Buffer page pointers
    u32 ext_buffer;             // Extended buffer (64-bit)
    u32 reserved[7];            // Padding for 32-byte alignment
} __attribute__((packed, aligned(32))) ehci_qh_t;

// Transfer Descriptor
typedef struct ehci_qtd {
    u32 horizontal_link;        // Next qTD (0x01 = terminate)
    u32 alt_next_qtd;           // Alternate next qTD (0x01 = terminate)
    u32 token;                  // Status + PID + length
    u32 buffer[5];              // Buffer page pointers (0 = terminate)
    u32 ext_buffer;             // Extended buffer (64-bit)
} __attribute__((packed, aligned(32))) ehci_qtd_t;

// qTD token bits
#define EHCI_QTD_ACTIVE         (1 << 7)
#define EHCI_QTD_HALTED         (1 << 6)
#define EHCI_QTD_BUFFER_ERROR   (1 << 5)
#define EHCI_QTD_BABBLE         (1 << 4)
#define EHCI_QTD_TRANSACTION_ERR (1 << 3)
#define EHCI_QTD_MISSED_MICRO   (1 << 2)
#define EHCI_QTD_PID_MASK       (0xFF << 8)
#define EHCI_QTD_LENGTH_MASK    (0x7FFF << 16)

#define EHCI_PID_OUT            0xE1
#define EHCI_PID_IN             0x69
#define EHCI_PID_SETUP          0x2D

// qTD link pointer bits
#define EHCI_LINK_TERMINATE     (1 << 0)
#define EHCI_LINK_TYP_QTD       (0 << 1)
#define EHCI_LINK_TYP_QH        (1 << 1)

// QH endpoint caps bits
#define EHCI_QH_DEV_ADDR_SHIFT  0
#define EHCI_QH_INACTIVE        (1 << 7)
#define EHCI_QH_EP_NUM_SHIFT    8
#define EHCI_QH_EP_SPEED_SHIFT  12
#define EHCI_QH_EP_SPEED_FULL   (0 << 12)
#define EHCI_QH_EP_SPEED_LOW    (1 << 12)
#define EHCI_QH_EP_SPEED_HIGH   (2 << 12)
#define EHCI_QH_DTC             (1 << 14)
#define EHCI_QH_MAX_PKT_SHIFT   16
#define EHCI_QH_CONTROL_EP      0
#define EHCI_QH_BULK_EP         1
#define EHCI_QH_INTERRUPT_EP    2

// ==================== EHCI CONTEXT ====================

typedef struct ehci_port {
    u32 num;
    bool connected;
    bool enabled;
    u8 speed;
    usb_device_t *device;
} ehci_port_t;

typedef struct ehci_transfer {
    usb_transfer_t *usb_xfer;
    ehci_qh_t *qh;
    ehci_qtd_t *setup_qtd;
    ehci_qtd_t *data_qtd;
    ehci_qtd_t *status_qtd;
    bool completed;
    u8 status;
    u32 transferred;
    struct list_head node;
} ehci_transfer_t;

typedef struct ehci_context {
    usb_hcd_t *hcd;
    volatile void *cap_regs;
    volatile void *op_regs;
    
    u32 caplength;
    u32 hcsparams;
    u32 hccparams;
    u32 ports;
    u32 companion_ports;
    
    u32 *periodic_list;
    u32 periodic_list_phys;
    ehci_qh_t *async_qh;
    ehci_qh_t *interrupt_qh;
    
    ehci_port_t ports_info[EHCI_MAX_PORTS];
    struct list_head pending_transfers;
    
    bool running;
    u8 irq;
    u32 gsi;
    u8 vector;
} ehci_context_t;

// ==================== FUNCTION PROTOTYPES ====================

int ehci_init(pci_device_t *pci_dev);
void ehci_irq_handler(usb_hcd_t *hcd);
void ehci_probe_all(void);

#endif