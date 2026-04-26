#ifndef XHCI_H
#define XHCI_H

#include <usb/usb.h>
#include <kernel/types.h>
#include <pci.h>

// PCI IDs
#define XHCI_PCI_CLASS      0x0C
#define XHCI_PCI_SUBCLASS   0x03
#define XHCI_PCI_PROG_IF    0x30

// ==================== CAPABILITY REGISTERS ====================
#define XHCI_CAPLENGTH      0x00
#define XHCI_HCIVERSION     0x02
#define XHCI_HCSPARAMS1     0x04
#define XHCI_HCSPARAMS2     0x08
#define XHCI_HCSPARAMS3     0x0C
#define XHCI_HCCPARAMS1     0x10
#define XHCI_DBOFF          0x14
#define XHCI_RTSOFF         0x18
#define XHCI_HCCPARAMS2     0x1C

// HCSPARAMS1 bits
#define XHCI_HCS1_DEVSLOT_MAX(n)    ((n) & 0xFF)
#define XHCI_HCS1_INTERRUPTERS_MAX  (((n) >> 8) & 0x7FF)
#define XHCI_HCS1_MAX_PORTS         ((n) >> 24)

// HCCPARAMS1 bits
#define XHCI_HCC1_AC64           (1 << 0)
#define XHCI_HCC1_BNC            (1 << 1)
#define XHCI_HCC1_CSZ            (1 << 2)
#define XHCI_HCC1_PPC            (1 << 3)
#define XHCI_HCC1_PIND           (1 << 4)
#define XHCI_HCC1_LHRC           (1 << 5)
#define XHCI_HCC1_LTC            (1 << 6)
#define XHCI_HCC1_NSS            (1 << 7)
#define XHCI_HCC1_PAE            (1 << 8)
#define XHCI_HCC1_SPC            (1 << 9)
#define XHCI_HCC1_SEC            (1 << 10)
#define XHCI_HCC1_CFC            (1 << 11)
#define XHCI_HCC1_MAX_PSA        (((n) >> 12) & 0x7)

// ==================== OPERATIONAL REGISTERS ====================
#define XHCI_USBCMD          0x00
#define XHCI_USBSTS          0x04
#define XHCI_PAGESIZE        0x08
#define XHCI_DNCTRL          0x14
#define XHCI_CRCR            0x18
#define XHCI_DCBAAP          0x30
#define XHCI_CONFIG          0x38

// USBCMD bits
#define XHCI_CMD_RS          (1 << 0)
#define XHCI_CMD_HCRST       (1 << 1)
#define XHCI_CMD_INTE        (1 << 2)
#define XHCI_CMD_HSEE        (1 << 3)
#define XHCI_CMD_LHCRST      (1 << 7)
#define XHCI_CMD_CSS         (1 << 8)
#define XHCI_CMD_CRS         (1 << 9)
#define XHCI_CMD_EWE         (1 << 10)
#define XHCI_CMD_EU3S        (1 << 11)

// USBSTS bits
#define XHCI_STS_HCH         (1 << 0)
#define XHCI_STS_HSE         (1 << 2)
#define XHCI_STS_EINT        (1 << 3)
#define XHCI_STS_PCD         (1 << 4)
#define XHCI_STS_SSS         (1 << 8)
#define XHCI_STS_RCS         (1 << 9)
#define XHCI_STS_SRE         (1 << 10)
#define XHCI_STS_CNR         (1 << 11)
#define XHCI_STS_HCE         (1 << 12)

// USBINTR bits
#define XHCI_INTR_PCD        (1 << 4)
#define XHCI_INTR_EINT       (1 << 3)

// ==================== PORT REGISTERS ====================
#define XHCI_PORTSC_BASE    0x400
#define XHCI_PORTSC_SIZE    0x10

#define XHCI_PORTSC_CCS      (1 << 0)
#define XHCI_PORTSC_PED      (1 << 1)
#define XHCI_PORTSC_OCA      (1 << 3)
#define XHCI_PORTSC_PR       (1 << 4)
#define XHCI_PORTSC_PP       (1 << 9)
#define XHCI_PORTSC_CSC      (1 << 17)
#define XHCI_PORTSC_PEC      (1 << 18)
#define XHCI_PORTSC_PRC      (1 << 21)
#define XHCI_PORTSC_WPR      (1 << 31)

// ==================== TRB TYPES ====================
#define XHCI_TRB_TYPE_NORMAL                1
#define XHCI_TRB_TYPE_SETUP                 2
#define XHCI_TRB_TYPE_DATA                  3
#define XHCI_TRB_TYPE_STATUS                4
#define XHCI_TRB_TYPE_ISOCH                 5
#define XHCI_TRB_TYPE_LINK                  6
#define XHCI_TRB_TYPE_EVENT_DATA            7
#define XHCI_TRB_TYPE_NO_OP                 8
#define XHCI_TRB_TYPE_ENABLE_SLOT           9
#define XHCI_TRB_TYPE_DISABLE_SLOT          10
#define XHCI_TRB_TYPE_ADDRESS_DEVICE        11
#define XHCI_TRB_TYPE_CONFIGURE_EP          12
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT      13
#define XHCI_TRB_TYPE_RESET_EP              14
#define XHCI_TRB_TYPE_STOP_EP               15
#define XHCI_TRB_TYPE_SET_TR_DEQUEUE        16
#define XHCI_TRB_TYPE_RESET_DEVICE          17
#define XHCI_TRB_TYPE_FORCE_EVENT           18
#define XHCI_TRB_TYPE_NEGOTIATE_BW          19
#define XHCI_TRB_TYPE_SET_LATENCY           20
#define XHCI_TRB_TYPE_GET_BW                21
#define XHCI_TRB_TYPE_FORCE_HEADER          22
#define XHCI_TRB_TYPE_NO_OP_CMD             23
#define XHCI_TRB_TYPE_GET_EXT_PROP          24
#define XHCI_TRB_TYPE_SET_EXT_PROP          25
#define XHCI_TRB_TYPE_TRANSFER_EVENT        32
#define XHCI_TRB_TYPE_CMD_COMPLETE_EVENT    33
#define XHCI_TRB_TYPE_PORT_STATUS_EVENT     34
#define XHCI_TRB_TYPE_BW_REQUEST_EVENT      35
#define XHCI_TRB_TYPE_DOORBELL_EVENT        36
#define XHCI_TRB_TYPE_HOST_CONTROLLER_EVENT 37
#define XHCI_TRB_TYPE_DEVICE_NOTIFY_EVENT   38
#define XHCI_TRB_TYPE_MFINDEX_WRAP_EVENT    39

// TRB flags
#define XHCI_TRB_CYCLE_BIT       (1 << 0)
#define XHCI_TRB_TC_BIT          (1 << 1)
#define XHCI_TRB_ENT_BIT         (1 << 5)
#define XHCI_TRB_ISP_BIT         (1 << 7)
#define XHCI_TRB_NS_BIT          (1 << 8)
#define XHCI_TRB_CH_BIT          (1 << 9)
#define XHCI_TRB_IOC_BIT         (1 << 10)
#define XHCI_TRB_IDT_BIT         (1 << 11)
#define XHCI_TRB_DIR_IN          (1 << 12)
#define XHCI_TRB_DIR_OUT         (0 << 12)

// Completion codes
#define XHCI_COMP_SUCCESS               1
#define XHCI_COMP_DATA_BUFFER_ERROR     2
#define XHCI_COMP_BABBLE_DETECTED       3
#define XHCI_COMP_TRB_ERROR             5
#define XHCI_COMP_STALL_ERROR           6
#define XHCI_COMP_RESOURCE_ERROR        7
#define XHCI_COMP_BANDWIDTH_ERROR       8
#define XHCI_COMP_NO_PORTS_AVAILABLE    9
#define XHCI_COMP_INVALID_STREAM_TYPE   10
#define XHCI_COMP_SLOT_NOT_ENABLED      11
#define XHCI_COMP_ENDPOINT_NOT_ENABLED  12
#define XHCI_COMP_SHORT_PACKET          13
#define XHCI_COMP_RING_UNDERRUN         14
#define XHCI_COMP_RING_OVERRUN          15
#define XHCI_COMP_VF_EVENT_RING_FULL    16
#define XHCI_COMP_PARAMETER_ERROR       17
#define XHCI_COMP_BANDWIDTH_OVERRUN     18
#define XHCI_COMP_CONTEXT_STATE_ERROR   19
#define XHCI_COMP_NO_PING_RESPONSE      20
#define XHCI_COMP_EVENT_RING_FULL       21
#define XHCI_COMP_INCOMPATIBLE_DEVICE   22
#define XHCI_COMP_MISSED_SERVICE        23
#define XHCI_COMP_COMMAND_RING_STOPPED  24
#define XHCI_COMP_COMMAND_ABORTED       25
#define XHCI_COMP_STOPPED               26
#define XHCI_COMP_STOPPED_LEN_INVALID   27

#define XHCI_USBINTR         0x0C
#define XHCI_ERSTBA          0x28
#define XHCI_ERSTS           0x2C
#define XHCI_ERDP            0x38

// ==================== STRUCTURES ====================

// TRB base structure
typedef struct {
    u32 parameter1;
    u32 parameter2;
    u32 status;
    u32 control;
} __attribute__((packed)) xhci_trb_t;

// Link TRB
typedef struct {
    u32 segment_ptr_low;
    u32 segment_ptr_high;
    u32 rsvd1:22;
    u32 interrupter_target:10;
    u32 cycle_bit:1;
    u32 toggle_cycle:1;
    u32 rsvd2:2;
    u32 chain_bit:1;
    u32 interrupt_on_completion:1;
    u32 rsvd3:4;
    u32 trb_type:6;
    u32 rsvd4:16;
} __attribute__((packed)) xhci_link_trb_t;

// Command: Enable Slot
typedef struct {
    u32 rsvd1[3];
    u32 rsvd2:9;
    u32 trb_type:6;
    u32 slot_type:5;
    u32 rsvd3:12;
} __attribute__((packed)) xhci_enable_slot_cmd_t;

// Command: Address Device
typedef struct {
    u32 input_ctx_ptr_low;
    u32 input_ctx_ptr_high;
    u32 rsvd1:24;
    u32 bsr:1;
    u32 trb_type:6;
    u32 rsvd2:8;
    u32 slot_id:8;
} __attribute__((packed)) xhci_address_device_cmd_t;

// Command: Configure Endpoint
typedef struct {
    u32 input_ctx_ptr_low;
    u32 input_ctx_ptr_high;
    u32 rsvd1:24;
    u32 deconfigure:1;
    u32 trb_type:6;
    u32 rsvd2:16;
    u32 slot_id:8;
} __attribute__((packed)) xhci_configure_ep_cmd_t;

// Transfer: Normal
typedef struct {
    u32 data_buffer_low;
    u32 data_buffer_high;
    u32 trb_length:17;
    u32 td_size:5;
    u32 interrupter:10;
    u32 cycle_bit:1;
    u32 eval_next:1;
    u32 isp:1;
    u32 nosnoop:1;
    u32 chain_bit:1;
    u32 ioc:1;
    u32 idt:1;
    u32 rsvd1:2;
    u32 bei:1;
    u32 trb_type:6;
    u32 rsvd2:16;
} __attribute__((packed)) xhci_normal_trb_t;

// Transfer: Setup Stage
typedef struct {
    u32 bmRequestType:8;
    u32 bRequest:8;
    u32 wValue:16;
    u32 wIndex:16;
    u32 wLength:16;
    u32 rsvd1:17;
    u32 rsvd2:5;
    u32 interrupter:10;
    u32 cycle_bit:1;
    u32 rsvd3:4;
    u32 ioc:1;
    u32 idt:1;
    u32 rsvd4:3;
    u32 trb_type:6;
    u32 transfer_type:2;
    u32 rsvd5:14;
} __attribute__((packed)) xhci_setup_trb_t;

// Transfer: Data Stage
typedef struct {
    u32 data_buffer_low;
    u32 data_buffer_high;
    u32 trb_length:17;
    u32 td_size:5;
    u32 interrupter:10;
    u32 cycle_bit:1;
    u32 eval_next:1;
    u32 isp:1;
    u32 nosnoop:1;
    u32 chain_bit:1;
    u32 ioc:1;
    u32 idt:1;
    u32 rsvd1:3;
    u32 trb_type:6;
    u32 direction:1;
    u32 rsvd2:15;
} __attribute__((packed)) xhci_data_trb_t;

// Transfer: Status Stage
typedef struct {
    u32 rsvd1[2];
    u32 rsvd2:22;
    u32 interrupter:10;
    u32 cycle_bit:1;
    u32 eval_next:1;
    u32 rsvd3:2;
    u32 chain_bit:1;
    u32 ioc:1;
    u32 rsvd4:4;
    u32 trb_type:6;
    u32 direction:1;
    u32 rsvd5:15;
} __attribute__((packed)) xhci_status_trb_t;

// Event: Transfer (from TRB)
typedef struct {
    u32 trb_pointer_low;
    u32 trb_pointer_high;
    u32 transfer_length:24;
    u32 completion_code:8;
    u32 cycle_bit:1;
    u32 rsvd1:1;
    u32 event_data:1;
    u32 rsvd2:7;
    u32 trb_type:6;
    u32 endpoint_id:5;
    u32 rsvd3:3;
    u32 slot_id:8;
} __attribute__((packed)) xhci_transfer_event_trb_t;

// Event: Command Completion (from TRB)
typedef struct {
    u32 cmd_trb_pointer_low;
    u32 cmd_trb_pointer_high;
    u32 cmd_completion_parm:24;
    u32 completion_code:8;
    u32 cycle_bit:1;
    u32 rsvd1:9;
    u32 trb_type:6;
    u32 vf_id:8;
    u32 slot_id:8;
} __attribute__((packed)) xhci_cmd_completion_event_trb_t;

// Event: Port Status Change (from TRB)
typedef struct {
    u32 rsvd1[2];
    u32 port_id:8;
    u32 rsvd2:24;
    u32 completion_code:8;
    u32 cycle_bit:1;
    u32 rsvd3:9;
    u32 trb_type:6;
    u32 rsvd4:8;
} __attribute__((packed)) xhci_port_status_event_trb_t;

typedef struct {
    u32 route_string:20;
    u32 speed:4;
    u32 rsvd1:1;
    u32 mtt:1;
    u32 hub:1;
    u32 context_entries:5;
    u32 rsvd2:16;
    u32 rsvd3:16;
    u32 max_exit_latency:16;
    u32 root_hub_port_num:8;
    u32 num_ports:8;
    u32 tt_hub_slot_id:8;
    u32 tt_port_num:8;
    u32 ttt:2;
    u32 rsvd4:4;
    u32 interrupter_target:10;
    u32 rsvd5:2;
    u32 device_address:8;
    u32 rsvd6:19;
    u32 slot_state:5;
} __attribute__((packed)) xhci_slot_ctx_t;

typedef struct {
    u32 ep_state:3;
    u32 rsvd1:5;
    u32 mult:2;
    u32 max_pstreams:5;
    u32 lsa:1;
    u32 interval:8;
    u32 rsvd2:8;
    u32 rsvd3:1;
    u32 error_count:2;
    u32 ep_type:3;
    u32 rsvd4:1;
    u32 host_init_disable:1;
    u32 max_burst_size:8;
    u32 max_packet_size:16;
    u32 dequeue_cycle:1;
    u32 rsvd5:3;
    u32 tr_dequeue_pointer_low;
    u32 tr_dequeue_pointer_high;
    u32 avg_trb_length:16;
    u32 max_esit_payload:16;
    u32 rsvd6[3];
} __attribute__((packed)) xhci_ep_ctx_t;

typedef struct {
    u64 ring_segment_base_address;
    u32 ring_segment_size:16;
    u32 rsvd1:16;
    u32 rsvd2;
} __attribute__((packed)) xhci_event_ring_segment_table_entry_t;

typedef struct xhci_ring {
    xhci_trb_t *trbs;
    u32 ring_phys;
    u32 enqueue;
    u32 dequeue;
    u32 cycle_state;
    u32 size;
} xhci_ring_t;

// ==================== CONTEXT STRUCTURES ====================

typedef struct {
    u32 drop_flags;
    u32 add_flags;
    u32 rsvd1[6];
    struct {
        u32 route_string:20;
        u32 speed:4;
        u32 rsvd1:1;
        u32 mtt:1;
        u32 hub:1;
        u32 context_entries:5;
        u32 rsvd2:16;
        u32 rsvd3:16;
        u32 max_exit_latency:16;
        u32 root_hub_port_num:8;
        u32 num_ports:8;
        u32 tt_hub_slot_id:8;
        u32 tt_port_num:8;
        u32 ttt:2;
        u32 rsvd4:4;
        u32 interrupter_target:10;
        u32 rsvd5:2;
        u32 device_address:8;
        u32 rsvd6:19;
        u32 slot_state:5;
    } __attribute__((packed)) slot;
    struct {
        u32 ep_state:3;
        u32 rsvd1:5;
        u32 mult:2;
        u32 max_pstreams:5;
        u32 lsa:1;
        u32 interval:8;
        u32 rsvd2:8;
        u32 rsvd3:1;
        u32 error_count:2;
        u32 ep_type:3;
        u32 rsvd4:1;
        u32 host_init_disable:1;
        u32 max_burst_size:8;
        u32 max_packet_size:16;
        u32 dequeue_cycle:1;
        u32 rsvd5:3;
        u32 tr_dequeue_pointer_low;
        u32 tr_dequeue_pointer_high;
        u32 avg_trb_length:16;
        u32 max_esit_payload:16;
        u32 rsvd6[3];
    } __attribute__((packed)) ep[31];
} __attribute__((packed, aligned(64))) xhci_input_context_t;

typedef struct {
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t ep[31];
} __attribute__((packed, aligned(64))) xhci_device_context_t;

// ==================== XHCI CONTEXT ====================

typedef struct {
    volatile void *cap_regs;
    volatile void *op_regs;
    volatile void *port_regs;
    volatile void *doorbell;
    volatile void *runtime_regs;
    
    u32 max_slots;
    u32 max_ports;
    
    u64 *dcbaa;
    u32 dcbaa_phys;
    
    xhci_ring_t cmd_ring;
    xhci_ring_t evt_ring;
    
    struct {
        u32 segment_phys;
        xhci_event_ring_segment_table_entry_t *segment;
        u32 size;
    } erst;
    
    struct {
        bool connected;
        bool enabled;
        u8 slot_id;
    } ports[32];
    
    u8 irq;
    u32 gsi;
    u8 vector;
    bool running;
    
    usb_hcd_t *hcd;
} xhci_ctx_t;

// ==================== FUNCTION PROTOTYPES ====================

int xhci_init(pci_device_t *pci_dev);
void xhci_probe_all(void);
void xhci_irq_handler(usb_hcd_t *hcd);

#endif