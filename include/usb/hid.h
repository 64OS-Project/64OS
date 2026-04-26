#ifndef USB_HID_H
#define USB_HID_H

#include <usb/usb.h>
#include <libk/string.h>

// HID class codes
#define HID_CLASS          0x03
#define HID_SUBCLASS_NONE  0x00
#define HID_SUBCLASS_BOOT  0x01
#define HID_PROTOCOL_NONE  0x00
#define HID_PROTOCOL_KEYBOARD 0x01
#define HID_PROTOCOL_MOUSE    0x02
#define HID_PROTOCOL_BOOT     0x03

// HID requests
#define HID_REQ_GET_REPORT      0x01
#define HID_REQ_GET_IDLE        0x02
#define HID_REQ_GET_PROTOCOL    0x03
#define HID_REQ_SET_REPORT      0x09
#define HID_REQ_SET_IDLE        0x0A
#define HID_REQ_SET_PROTOCOL    0x0B

// HID report types
#define HID_REPORT_INPUT        0x01
#define HID_REPORT_OUTPUT       0x02
#define HID_REPORT_FEATURE      0x03

// HID item tags (short items)
#define HID_MAIN_TAG            0x80
#define HID_GLOBAL_TAG          0x40
#define HID_LOCAL_TAG           0x00

// Main tags (bits 2-4)
#define HID_MAIN_INPUT          0x80
#define HID_MAIN_OUTPUT         0x90
#define HID_MAIN_FEATURE        0xB0
#define HID_MAIN_COLLECTION     0xA0
#define HID_MAIN_END_COLLECTION 0xC0

// Global tags
#define HID_GLOBAL_USAGE_PAGE   0x04
#define HID_GLOBAL_LOGICAL_MIN  0x14
#define HID_GLOBAL_LOGICAL_MAX  0x24
#define HID_GLOBAL_PHYSICAL_MIN 0x34
#define HID_GLOBAL_PHYSICAL_MAX 0x44
#define HID_GLOBAL_UNIT_EXPONENT 0x54
#define HID_GLOBAL_UNIT         0x64
#define HID_GLOBAL_REPORT_SIZE  0x74
#define HID_GLOBAL_REPORT_ID    0x84
#define HID_GLOBAL_REPORT_COUNT 0x94
#define HID_GLOBAL_PUSH         0xA4
#define HID_GLOBAL_POP          0xB4

// Local tags
#define HID_LOCAL_USAGE         0x08
#define HID_LOCAL_USAGE_MIN     0x18
#define HID_LOCAL_USAGE_MAX     0x28
#define HID_LOCAL_DESIGNATOR_INDEX 0x38
#define HID_LOCAL_DESIGNATOR_MIN   0x48
#define HID_LOCAL_DESIGNATOR_MAX   0x58
#define HID_LOCAL_STRING_INDEX    0x78
#define HID_LOCAL_STRING_MIN      0x88
#define HID_LOCAL_STRING_MAX      0x98
#define HID_LOCAL_DELIMITER       0xA8

// Collection types
#define HID_COLLECTION_PHYSICAL  0x00
#define HID_COLLECTION_APPLICATION 0x01
#define HID_COLLECTION_LOGICAL   0x02
#define HID_COLLECTION_REPORT    0x03
#define HID_COLLECTION_NAMED_ARRAY 0x04
#define HID_COLLECTION_USAGE_SWITCH 0x05
#define HID_COLLECTION_USAGE_MODIFIER 0x06

// Input/Output flags
#define HID_IO_DATA              0x00
#define HID_IO_CONSTANT          0x01
#define HID_IO_ARRAY             0x00
#define HID_IO_VARIABLE          0x02
#define HID_IO_ABSOLUTE          0x00
#define HID_IO_RELATIVE          0x04
#define HID_IO_NO_WRAP           0x00
#define HID_IO_WRAP              0x08
#define HID_IO_LINEAR            0x00
#define HID_IO_NONLINEAR         0x10
#define HID_IO_PREFERRED_STATE   0x00
#define HID_IO_NO_PREFERRED      0x20
#define HID_IO_NULL_STATE        0x00
#define HID_IO_NON_NULL          0x40
#define HID_IO_BUFFERED_BYTES    0x00
#define HID_IO_BUFFERED_BITS     0x80

// Mouse button bits
#define HID_MOUSE_BUTTON_1       0x01
#define HID_MOUSE_BUTTON_2       0x02
#define HID_MOUSE_BUTTON_3       0x04
#define HID_MOUSE_BUTTON_4       0x08
#define HID_MOUSE_BUTTON_5       0x10

// HID report item
typedef struct hid_report_item {
    u32 usage_page;
    u32 usage;
    u32 logical_min;
    u32 logical_max;
    u32 physical_min;
    u32 physical_max;
    u32 unit;
    u32 unit_exponent;
    u32 report_size;
    u32 report_count;
    u32 report_id;
    u32 flags;
    u8 collection_depth;
    
    u32 offset_bits;
    u32 size_bits;
    
    struct hid_report_item *next;
} hid_report_item_t;

// HID report
typedef struct hid_report {
    u32 id;
    u32 size_bits;
    u32 size_bytes;
    hid_report_item_t *items;
    struct hid_report *next;
} hid_report_t;

// HID device
typedef struct hid_device {
    usb_device_t *usb_dev;
    u8 interface;
    u8 protocol;
    u8 boot_protocol;
    
    u8 *report_descriptor;
    u32 report_desc_size;
    
    hid_report_t *reports;
    u8 *current_data;
    u32 current_data_size;
    
    void (*input_report_callback)(struct hid_device *hid, u8 *data, u32 len);
    void (*output_report_callback)(struct hid_device *hid, u8 *data, u32 len);
    void *private;

    u32 current_usage_page;
    u32 current_logical_min;
    u32 current_logical_max;
    u32 current_physical_min;
    u32 current_physical_max;
    u32 current_unit_exponent;
    u32 current_unit;
    u32 current_report_size;
    u32 current_report_count;
    u32 current_report_id;
    u32 current_usage_index;
    u32 current_usages[32];
    hid_report_t *current_report;
} hid_device_t;

// HID driver
typedef struct hid_driver {
    char name[32];
    u16 vendor_id;
    u16 product_id;
    u32 usage_page;
    u32 usage;
    int (*probe)(hid_device_t *hid);
    void (*disconnect)(hid_device_t *hid);
    struct hid_driver *next;
} hid_driver_t;

// ==================== HID CORE FUNCTIONS ====================

void hid_init(void);
int hid_register_driver(hid_driver_t *drv);
int hid_device_probe(usb_device_t *usb_dev, u8 interface, u8 protocol);
void hid_device_disconnect(hid_device_t *hid);

int hid_get_report_descriptor(hid_device_t *hid);
int hid_parse_report_descriptor(hid_device_t *hid);
void hid_free_reports(hid_device_t *hid);

int hid_set_protocol(hid_device_t *hid, u8 protocol);
int hid_set_idle(hid_device_t *hid, u8 duration);
int hid_get_report(hid_device_t *hid, u8 type, u8 id, u8 *data, u16 len);
int hid_set_report(hid_device_t *hid, u8 type, u8 id, u8 *data, u16 len);

int hid_start_input(hid_device_t *hid);
int hid_stop_input(hid_device_t *hid);

// Report parsing helpers
u32 hid_parse_item(u8 *desc, u32 pos, u32 *type, u32 *tag, u32 *size, u32 *data);

#endif