#include <usb/hid.h>
#include <usb/usb.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>

static hid_driver_t *g_hid_drivers = NULL;

// ==================== HID REGISTRATION ====================

void hid_init(void) {
    terminal_printf("[HID] Core initialized\n");
}

int hid_register_driver(hid_driver_t *drv) {
    if (!drv) return -1;
    
    drv->next = g_hid_drivers;
    g_hid_drivers = drv;
    
    terminal_printf("[HID] Registered driver: %s\n", drv->name);
    return 0;
}

// ==================== REPORT DESCRIPTOR PARSING ====================

u32 hid_parse_item(u8 *desc, u32 pos, u32 *type, u32 *tag, u32 *size, u32 *data) {
    u8 byte = desc[pos];
    *type = (byte >> 2) & 0x03;
    *tag = (byte >> 4) & 0x0F;
    
    u32 data_size = byte & 0x03;
    
    pos++;
    
    *data = 0;
    
    if (data_size == 1) {
        *data = desc[pos];
        pos++;
    } else if (data_size == 2) {
        *data = desc[pos] | (desc[pos+1] << 8);
        pos += 2;
    } else if (data_size == 3) {
        *data = desc[pos] | (desc[pos+1] << 8) | (desc[pos+2] << 16);
        pos += 3;
    }
    
    *size = data_size + 1;
    
    return pos;
}

static hid_report_t *hid_create_report(u32 id) {
    hid_report_t *report = (hid_report_t*)malloc(sizeof(hid_report_t));
    if (!report) return NULL;
    
    memset(report, 0, sizeof(hid_report_t));
    report->id = id;
    return report;
}

static hid_report_item_t *hid_create_report_item(void) {
    hid_report_item_t *item = (hid_report_item_t*)malloc(sizeof(hid_report_item_t));
    if (!item) return NULL;
    
    memset(item, 0, sizeof(hid_report_item_t));
    return item;
}

static void hid_add_report_item(hid_report_t *report, hid_report_item_t *item) {
    if (!report->items) {
        report->items = item;
    } else {
        hid_report_item_t *last = report->items;
        while (last->next) last = last->next;
        last->next = item;
    }
    
    report->size_bits += item->size_bits;
}

static void hid_parse_global_item(hid_device_t *hid, u32 tag, u32 data) {
    switch (tag) {
        case 0: hid->current_usage_page = data; break;
        case 1: hid->current_logical_min = data; break;
        case 2: hid->current_logical_max = data; break;
        case 3: hid->current_physical_min = data; break;
        case 4: hid->current_physical_max = data; break;
        case 5: hid->current_unit_exponent = data; break;
        case 6: hid->current_unit = data; break;
        case 7: hid->current_report_size = data; break;
        case 8: hid->current_report_id = data; break;
        case 9: hid->current_report_count = data; break;
    }
}

static void hid_parse_local_item(hid_device_t *hid, u32 tag, u32 data) {
    switch (tag) {
        case 0:
            if (hid->current_usage_index < 32) {
                hid->current_usages[hid->current_usage_index++] = data;
            }
            break;
    }
}

static void hid_parse_main_item(hid_device_t *hid, u32 tag, u32 data) {
    if (tag == 8) { // INPUT
        hid_report_t *report = hid->current_report;
        if (!report) return;
        
        if (report->id == 0 && hid->current_report_id != 0) {
            report->id = hid->current_report_id;
        }
        
        u32 count = hid->current_report_count;
        u32 size = hid->current_report_size;
        
        for (u32 i = 0; i < count; i++) {
            hid_report_item_t *item = hid_create_report_item();
            if (!item) continue;
            
            item->usage_page = hid->current_usage_page;
            if (hid->current_usage_index > i) {
                item->usage = hid->current_usages[i];
            } else if (hid->current_usage_index > 0) {
                item->usage = hid->current_usages[0];
            } else {
                item->usage = 0;
            }
            
            item->logical_min = hid->current_logical_min;
            item->logical_max = hid->current_logical_max;
            item->physical_min = hid->current_physical_min;
            item->physical_max = hid->current_physical_max;
            item->report_size = size;
            item->report_count = 1;
            item->report_id = hid->current_report_id;
            item->flags = data;
            item->size_bits = size;
            item->offset_bits = report->size_bits;
            
            if (item->usage_page == 0x01 && item->usage >= 0x80 && item->usage <= 0x83) {
                // Mouse buttons
                terminal_printf("[HID] Mouse button usage 0x%x\n", item->usage);
            } else if (item->usage_page == 0x01 && item->usage == 0x30) {
                terminal_printf("[HID] Mouse X axis\n");
            } else if (item->usage_page == 0x01 && item->usage == 0x31) {
                terminal_printf("[HID] Mouse Y axis\n");
            }
            
            hid_add_report_item(report, item);
        }
    }
}

int hid_parse_report_descriptor(hid_device_t *hid) {
    if (!hid || !hid->report_descriptor) return -1;
    
    u32 pos = 0;
    u32 collection_depth = 0;
    
    hid->current_usage_page = 0;
    hid->current_logical_min = 0;
    hid->current_logical_max = 0;
    hid->current_physical_min = 0;
    hid->current_physical_max = 0;
    hid->current_report_size = 8;
    hid->current_report_count = 1;
    hid->current_report_id = 0;
    hid->current_usage_index = 0;
    memset(hid->current_usages, 0, sizeof(hid->current_usages));
    
    hid->current_report = NULL;
    hid->reports = NULL;
    
    while (pos < hid->report_desc_size) {
        u32 type, tag, size, data;
        u32 new_pos = hid_parse_item(hid->report_descriptor, pos, &type, &tag, &size, &data);
        
        if (type == 0) { // MAIN
            switch (tag) {
                case 8:  // INPUT
                case 9:  // OUTPUT
                case 11: // FEATURE
                    hid_parse_main_item(hid, tag, data);
                    break;
                case 10: // COLLECTION
                    collection_depth++;
                    break;
                case 12: // END COLLECTION
                    if (collection_depth > 0) collection_depth--;
                    break;
                default:
                    break;
            }
        } else if (type == 1) { // GLOBAL
            hid_parse_global_item(hid, tag, data);
        } else if (type == 2) { // LOCAL
            hid_parse_local_item(hid, tag, data);
        }
        
        pos = new_pos;
    }
    
    // Calculate report sizes
    for (hid_report_t *report = hid->reports; report; report = report->next) {
        report->size_bytes = (report->size_bits + 7) / 8;
        terminal_printf("[HID] Report %d: %d bits (%d bytes)\n", 
                       report->id, report->size_bits, report->size_bytes);
    }
    
    return 0;
}

// ==================== HID DEVICE MANAGEMENT ====================

int hid_get_report_descriptor(hid_device_t *hid) {
    if (!hid || !hid->usb_dev) return -1;
    
    // Get report descriptor size first
    u16 desc_size = 0;
    u8 buf[256];
    
    // Standard request to get descriptor length
    int ret = usb_control_transfer(hid->usb_dev,
        USB_DIR_IN | USB_RECIP_INTERFACE,
        USB_REQ_GET_DESCRIPTOR,
        0x22 << 8, // HID report descriptor type
        hid->interface,
        sizeof(buf), buf);
    
    if (ret < 0) return -1;
    
    hid->report_desc_size = ret;
    hid->report_descriptor = (u8*)malloc(hid->report_desc_size);
    if (!hid->report_descriptor) return -1;
    
    memcpy(hid->report_descriptor, buf, hid->report_desc_size);
    
    terminal_printf("[HID] Report descriptor: %d bytes\n", hid->report_desc_size);
    
    return hid_parse_report_descriptor(hid);
}

int hid_set_protocol(hid_device_t *hid, u8 protocol) {
    return usb_control_transfer(hid->usb_dev,
        USB_DIR_OUT | USB_RECIP_INTERFACE,
        HID_REQ_SET_PROTOCOL,
        protocol,
        hid->interface,
        0, NULL);
}

int hid_set_idle(hid_device_t *hid, u8 duration) {
    return usb_control_transfer(hid->usb_dev,
        USB_DIR_OUT | USB_RECIP_INTERFACE,
        HID_REQ_SET_IDLE,
        duration << 8,
        hid->interface,
        0, NULL);
}

int hid_start_input(hid_device_t *hid) {
    if (!hid || !hid->usb_dev) return -1;
    
    // Find interrupt IN endpoint
    u8 ep_addr = 0;
    for (u32 i = 0; i < hid->usb_dev->endpoint_count; i++) {
        if ((hid->usb_dev->endpoints[i]->attributes & 0x03) == 3 && // Interrupt
            (hid->usb_dev->endpoints[i]->direction == USB_DIR_IN)) {
            ep_addr = hid->usb_dev->endpoints[i]->address;
            break;
        }
    }
    
    if (!ep_addr) return -1;
    
    // Find report size
    u32 report_size = 8;
    if (hid->reports) {
        report_size = hid->reports->size_bytes;
    }
    
    hid->current_data_size = report_size;
    hid->current_data = (u8*)malloc(report_size);
    if (!hid->current_data) return -1;
    
    // TODO: Setup interrupt transfer
    terminal_printf("[HID] Started input on endpoint 0x%x (size=%d)\n", ep_addr, report_size);
    
    return 0;
}

// ==================== HID DRIVER PROBE ====================

int hid_device_probe(usb_device_t *usb_dev, u8 interface, u8 protocol) {
    terminal_printf("[HID] Probing device %04X:%04X interface %d\n",
                   usb_dev->vendor_id, usb_dev->product_id, interface);
    
    hid_device_t *hid = (hid_device_t*)malloc(sizeof(hid_device_t));
    if (!hid) return -1;
    
    memset(hid, 0, sizeof(hid_device_t));
    hid->usb_dev = usb_dev;
    hid->interface = interface;
    hid->protocol = protocol;
    
    // Get report descriptor
    if (hid_get_report_descriptor(hid) != 0) {
        terminal_error_printf("[HID] Failed to get report descriptor\n");
        free(hid);
        return -1;
    }
    
    // Set boot protocol if requested
    if (protocol == HID_PROTOCOL_BOOT) {
        hid_set_protocol(hid, 0);
        hid_set_idle(hid, 0);
    }
    
    // Find matching driver
    hid_driver_t *drv = g_hid_drivers;
    while (drv) {
        if ((drv->vendor_id == 0 || drv->vendor_id == usb_dev->vendor_id) &&
            (drv->product_id == 0 || drv->product_id == usb_dev->product_id)) {
            
            if (drv->probe && drv->probe(hid) == 0) {
                terminal_printf("[HID] Driver %s claimed device\n", drv->name);
                hid->private = drv;
                
                // Start input reports
                hid_start_input(hid);
                return 0;
            }
        }
        drv = drv->next;
    }
    
    terminal_warn_printf("[HID] No driver for device\n");
    return -1;
}