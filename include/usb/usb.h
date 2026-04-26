#ifndef USB_H
#define USB_H

#include <kernel/types.h>
#include <kernel/list.h>

// ==================== USB CONSTANTS ====================

// USB versions
#define USB_1_0         0x0100
#define USB_1_1         0x0110
#define USB_2_0         0x0200
#define USB_3_0         0x0300

// Transfer types
#define USB_TRANSFER_CONTROL      0
#define USB_TRANSFER_ISOCHRONOUS  1
#define USB_TRANSFER_BULK         2
#define USB_TRANSFER_INTERRUPT    3

// Endpoint directions
#define USB_DIR_OUT        0
#define USB_DIR_IN         1
#define USB_DIR_BOTH       2

// Endpoint types
#define USB_ENDPOINT_CONTROL    0
#define USB_ENDPOINT_ISOCH      1
#define USB_ENDPOINT_BULK       2
#define USB_ENDPOINT_INTERRUPT  3

// Device classes
#define USB_CLASS_AUDIO         0x01
#define USB_CLASS_COMM          0x02
#define USB_CLASS_HID           0x03
#define USB_CLASS_PHYSICAL      0x05
#define USB_CLASS_IMAGE         0x06
#define USB_CLASS_PRINTER       0x07
#define USB_CLASS_MASS_STORAGE  0x08
#define USB_CLASS_HUB           0x09
#define USB_CLASS_CDC_DATA      0x0A
#define USB_CLASS_SMART_CARD    0x0B
#define USB_CLASS_CONTENT_SEC   0x0D
#define USB_CLASS_VIDEO         0x0E
#define USB_CLASS_PERSONAL_HC   0x0F
#define USB_CLASS_VENDOR_SPEC   0xFF

// HID subclasses
#define USB_HID_SUBCLASS_BOOT   0x01

// HID protocols
#define USB_HID_PROTOCOL_NONE   0x00
#define USB_HID_PROTOCOL_KEYBOARD 0x01
#define USB_HID_PROTOCOL_MOUSE    0x02

// Request types
#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_DESCRIPTOR      0x07
#define USB_REQ_GET_CONFIGURATION   0x08
#define USB_REQ_SET_CONFIGURATION   0x09
#define USB_REQ_GET_INTERFACE       0x0A
#define USB_REQ_SET_INTERFACE       0x0B
#define USB_REQ_SYNCH_FRAME         0x0C

// Descriptor types
#define USB_DESC_DEVICE         0x01
#define USB_DESC_CONFIGURATION  0x02
#define USB_DESC_STRING         0x03
#define USB_DESC_INTERFACE      0x04
#define USB_DESC_ENDPOINT       0x05
#define USB_DESC_DEVICE_QUALIFIER 0x06
#define USB_DESC_OTHER_SPEED    0x07
#define USB_DESC_INTERFACE_POWER 0x08
#define USB_DESC_HUB            0x29

// Request recipient
#define USB_RECIP_DEVICE       0x00
#define USB_RECIP_INTERFACE    0x01
#define USB_RECIP_ENDPOINT     0x02
#define USB_RECIP_OTHER        0x03

// ==================== USB DESCRIPTORS ====================

typedef struct {
    u8 bLength;
    u8 bDescriptorType;
    u16 bcdUSB;
    u8 bDeviceClass;
    u8 bDeviceSubClass;
    u8 bDeviceProtocol;
    u8 bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8 iManufacturer;
    u8 iProduct;
    u8 iSerialNumber;
    u8 bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct {
    u8 bLength;
    u8 bDescriptorType;
    u16 wTotalLength;
    u8 bNumInterfaces;
    u8 bConfigurationValue;
    u8 iConfiguration;
    u8 bmAttributes;
    u8 bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

typedef struct {
    u8 bLength;
    u8 bDescriptorType;
    u8 bInterfaceNumber;
    u8 bAlternateSetting;
    u8 bNumEndpoints;
    u8 bInterfaceClass;
    u8 bInterfaceSubClass;
    u8 bInterfaceProtocol;
    u8 iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

typedef struct {
    u8 bLength;
    u8 bDescriptorType;
    u8 bEndpointAddress;
    u8 bmAttributes;
    u16 wMaxPacketSize;
    u8 bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

typedef struct {
    u8 bLength;
    u8 bDescriptorType;
    u16 wData[];
} __attribute__((packed)) usb_string_descriptor_t;

// ==================== USB REQUEST ====================

typedef struct {
    u8 bmRequestType;
    u8 bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} __attribute__((packed)) usb_setup_packet_t;

// ==================== USB TRANSFER ====================

typedef struct usb_transfer {
    u8 endpoint;
    u8 direction;
    u8 type;
    u32 length;
    u8 *buffer;
    u32 timeout_ms;
    int status;
    void (*callback)(struct usb_transfer *transfer);
    void *context;
    struct list_head node;
    struct usb_device *device;
} usb_transfer_t;

// ==================== USB DEVICE ====================

struct usb_driver;

typedef struct usb_device {
    struct usb_hcd *hcd;           // Host controller driver
    u8 address;                     // USB address (1-127)
    u8 speed;                       // Low/Full/High/Super
    u16 vendor_id;
    u16 product_id;
    u16 bcd_device;
    u8 device_class;
    u8 device_subclass;
    u8 device_protocol;
    u8 max_packet_size0;
    
    // Device descriptors (cached)
    usb_device_descriptor_t *device_desc;
    usb_config_descriptor_t *config_desc;
    void *config_data;              // Full configuration data
    u32 config_len;
    
    // Current state
    u8 configuration;
    u8 interface;
    u8 alternate_setting;
    
    // Endpoints
    struct usb_endpoint *ep0;       // Control endpoint
    struct usb_endpoint **endpoints;
    u32 endpoint_count;
    
    // Driver
    struct usb_driver *driver;
    void *driver_data;
    
    // For device tree
    struct usb_device *parent;
    struct list_head childs;
    struct list_head node;
} usb_device_t;

// ==================== USB ENDPOINT ====================

typedef struct usb_endpoint {
    u8 address;
    u8 attributes;
    u16 max_packet_size;
    u8 interval;
    u8 type;        // control/bulk/interrupt/isoch
    u8 direction;   // IN/OUT
    usb_device_t *device;
    
    // Queue
    struct list_head transfer_queue;
    bool halted;
} usb_endpoint_t;

// ==================== USB DRIVER ====================

typedef struct usb_driver {
    char name[32];
    u16 vendor_id;
    u16 product_id;
    u8 device_class;
    u8 device_subclass;
    
    int (*probe)(usb_device_t *dev);
    int (*disconnect)(usb_device_t *dev);
    int (*reset)(usb_device_t *dev);
    int (*suspend)(usb_device_t *dev);
    int (*resume)(usb_device_t *dev);
    
    struct list_head node;
} usb_driver_t;

// ==================== USB HOST CONTROLLER ====================

typedef struct usb_hcd_ops {
    int (*init)(struct usb_hcd *hcd);
    int (*shutdown)(struct usb_hcd *hcd);
    int (*reset)(struct usb_hcd *hcd);
    
    int (*submit_transfer)(struct usb_hcd *hcd, usb_transfer_t *transfer);
    int (*cancel_transfer)(struct usb_hcd *hcd, usb_transfer_t *transfer);
    
    int (*set_address)(struct usb_hcd *hcd, usb_device_t *dev);
    int (*enable_endpoint)(struct usb_hcd *hcd, usb_endpoint_t *ep);
    int (*disable_endpoint)(struct usb_hcd *hcd, usb_endpoint_t *ep);
    
    void (*irq_handler)(struct usb_hcd *hcd);
} usb_hcd_ops_t;

typedef struct usb_hcd {
    char name[32];
    u16 vendor_id;
    u16 device_id;
    u32 mmio_base;
    u8 irq;
    u32 gsi;
    void *regs;
    
    usb_hcd_ops_t *ops;
    void *private;
    
    // USB devices connected
    struct list_head devices;
    u32 device_count;
    u8 next_address;
    bool running;
    
    struct list_head node;
} usb_hcd_t;

// ==================== USB CORE API ====================

// Initialization
void usb_init(void);
int usb_hcd_register(usb_hcd_t *hcd);
int usb_driver_register(usb_driver_t *drv);

// Device management
usb_device_t *usb_device_alloc(usb_hcd_t *hcd);
void usb_device_free(usb_device_t *dev);
int usb_device_add(usb_hcd_t *hcd, usb_device_t *dev);
int usb_device_remove(usb_device_t *dev);
usb_device_t *usb_device_find(u16 vendor_id, u16 product_id);

// Transfers
int usb_control_transfer(usb_device_t *dev, u8 request_type, u8 request,
                         u16 value, u16 index, u16 length, void *data);
int usb_bulk_transfer(usb_device_t *dev, u8 endpoint, void *data, u32 length);
int usb_interrupt_transfer(usb_device_t *dev, u8 endpoint, void *data, u32 length);
int usb_isochronous_transfer(usb_device_t *dev, u8 endpoint, void *data, u32 length);

// Descriptor parsing
int usb_get_descriptor(usb_device_t *dev, u8 type, u8 index, void *buf, u16 len);
int usb_get_device_descriptor(usb_device_t *dev, usb_device_descriptor_t *desc);
int usb_get_config_descriptor(usb_device_t *dev, u8 index, void *buf, u16 len);
int usb_get_string_descriptor(usb_device_t *dev, u8 index, char *buf, u16 len);

// Enumeration
int usb_enumeration(usb_device_t *dev);
int usb_set_address(usb_device_t *dev, u8 address);
int usb_set_configuration(usb_device_t *dev, u8 configuration);

// Hub handling
void usb_hub_init(void);
void usb_handle_port_event(usb_hcd_t *hcd, u8 port);

// ==================== USB 64OS ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================

static inline const char *usb_speed_string(u8 speed) {
    switch (speed) {
        case 0: return "Low";
        case 1: return "Full";
        case 2: return "High";
        case 3: return "Super";
        default: return "Unknown";
    }
}

static inline const char *usb_class_string(u8 class) {
    switch (class) {
        case USB_CLASS_AUDIO: return "Audio";
        case USB_CLASS_COMM: return "Communication";
        case USB_CLASS_HID: return "HID";
        case USB_CLASS_MASS_STORAGE: return "Mass Storage";
        case USB_CLASS_HUB: return "Hub";
        case USB_CLASS_PRINTER: return "Printer";
        case USB_CLASS_VENDOR_SPEC: return "Vendor Specific";
        default: return "Unknown";
    }
}

#endif