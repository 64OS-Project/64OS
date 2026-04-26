#include <usb/usb.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>

static struct list_head g_usb_hcds;
static struct list_head g_usb_drivers;
static struct list_head g_usb_devices;
static u32 g_usb_init_refcount = 0;

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

void usb_init(void) {
    if (g_usb_init_refcount++ > 0) return;
    
    list_init(&g_usb_hcds);
    list_init(&g_usb_drivers);
    list_init(&g_usb_devices);
    
    terminal_printf("[USB] Core initialized\n");
}

// ==================== HCD REGISTRATION ====================

int usb_hcd_register(usb_hcd_t *hcd) {
    if (!hcd || !hcd->ops) return -1;
    
    list_add_tail(&g_usb_hcds, &hcd->node);
    
    if (hcd->ops->init) {
        if (hcd->ops->init(hcd) != 0) {
            terminal_error_printf("[USB] Failed to init HCD %s\n", hcd->name);
            list_del(&hcd->node);
            return -1;
        }
    }
    
    terminal_printf("[USB] HCD registered: %s (IRQ %d)\n", hcd->name, hcd->irq);
    return 0;
}

// ==================== DRIVER REGISTRATION ====================

int usb_driver_register(usb_driver_t *drv) {
    if (!drv) return -1;
    
    list_add_tail(&g_usb_drivers, &drv->node);
    terminal_printf("[USB] Driver registered: %s\n", drv->name);
    
    // Try to bind to existing devices
    struct list_head *pos;
    list_for_each(pos, &g_usb_devices) {
        usb_device_t *dev = list_entry(pos, usb_device_t, node);
        if (dev->driver) continue;
        
        if (drv->vendor_id && drv->vendor_id != dev->vendor_id) continue;
        if (drv->product_id && drv->product_id != dev->product_id) continue;
        if (drv->device_class && drv->device_class != dev->device_class) continue;
        
        if (drv->probe && drv->probe(dev) == 0) {
            dev->driver = drv;
            terminal_printf("[USB] Device %04X:%04X bound to %s\n",
                          dev->vendor_id, dev->product_id, drv->name);
        }
    }
    
    return 0;
}

// ==================== DEVICE MANAGEMENT ====================

usb_device_t *usb_device_alloc(usb_hcd_t *hcd) {
    usb_device_t *dev = (usb_device_t*)malloc(sizeof(usb_device_t));
    if (!dev) return NULL;
    
    memset(dev, 0, sizeof(usb_device_t));
    dev->hcd = hcd;
    list_init(&dev->childs);
    
    return dev;
}

void usb_device_free(usb_device_t *dev) {
    if (!dev) return;
    
    if (dev->device_desc) free(dev->device_desc);
    if (dev->config_data) free(dev->config_data);
    if (dev->endpoints) free(dev->endpoints);
    
    free(dev);
}

int usb_device_add(usb_hcd_t *hcd, usb_device_t *dev) {
    if (!hcd || !dev) return -1;
    
    dev->address = ++hcd->next_address;
    if (dev->address > 127) hcd->next_address = 1;
    
    list_add_tail(&g_usb_devices, &dev->node);
    list_add_tail(&hcd->devices, &dev->node);
    hcd->device_count++;
    
    // Try to bind driver
    struct list_head *pos;
    list_for_each(pos, &g_usb_drivers) {
        usb_driver_t *drv = list_entry(pos, usb_driver_t, node);
        if (drv->vendor_id && drv->vendor_id != dev->vendor_id) continue;
        if (drv->product_id && drv->product_id != dev->product_id) continue;
        
        if (drv->probe && drv->probe(dev) == 0) {
            dev->driver = drv;
            break;
        }
    }
    
    return 0;
}

int usb_device_remove(usb_device_t *dev) {
    if (!dev) return -1;
    
    if (dev->driver && dev->driver->disconnect) {
        dev->driver->disconnect(dev);
    }
    
    list_del(&dev->node);
    if (dev->hcd) {
        list_del(&dev->node);
        dev->hcd->device_count--;
    }
    
    usb_device_free(dev);
    return 0;
}

usb_device_t *usb_device_find(u16 vendor_id, u16 product_id) {
    struct list_head *pos;
    list_for_each(pos, &g_usb_devices) {
        usb_device_t *dev = list_entry(pos, usb_device_t, node);
        if (dev->vendor_id == vendor_id && dev->product_id == product_id) {
            return dev;
        }
    }
    return NULL;
}

// ==================== CONTROL TRANSFERS ====================

int usb_control_transfer(usb_device_t *dev, u8 request_type, u8 request,
                         u16 value, u16 index, u16 length, void *data) {
    if (!dev || !dev->hcd || !dev->hcd->ops->submit_transfer) return -1;
    
    usb_setup_packet_t setup;
    setup.bmRequestType = request_type;
    setup.bRequest = request;
    setup.wValue = value;
    setup.wIndex = index;
    setup.wLength = length;
    
    // Allocate transfer
    usb_transfer_t *transfer = (usb_transfer_t*)malloc(sizeof(usb_transfer_t));
    if (!transfer) return -1;
    
    memset(transfer, 0, sizeof(usb_transfer_t));
    transfer->endpoint = 0;
    transfer->direction = (request_type & USB_DIR_IN) ? USB_DIR_IN : USB_DIR_OUT;
    transfer->type = USB_TRANSFER_CONTROL;
    transfer->length = length;
    transfer->buffer = data;
    transfer->timeout_ms = 1000;
    
    // For control transfers, need to send setup packet first
    // This is HCD specific, so we pass setup packet in context
    transfer->context = &setup;
    
    int ret = dev->hcd->ops->submit_transfer(dev->hcd, transfer);
    
    // Wait for completion (simplified)
    for (int i = 0; i < 1000000 && transfer->status == 0; i++) {
        asm volatile("pause");
    }
    
    ret = transfer->status;
    free(transfer);
    
    return ret;
}

// ==================== BULK TRANSFERS ====================

int usb_bulk_transfer(usb_device_t *dev, u8 endpoint, void *data, u32 length) {
    if (!dev || !dev->hcd || !dev->hcd->ops->submit_transfer) return -1;
    
    usb_transfer_t *transfer = (usb_transfer_t*)malloc(sizeof(usb_transfer_t));
    if (!transfer) return -1;
    
    memset(transfer, 0, sizeof(usb_transfer_t));
    transfer->endpoint = endpoint & 0x7F;
    transfer->direction = (endpoint & 0x80) ? USB_DIR_IN : USB_DIR_OUT;
    transfer->type = USB_TRANSFER_BULK;
    transfer->length = length;
    transfer->buffer = data;
    transfer->timeout_ms = 5000;
    
    int ret = dev->hcd->ops->submit_transfer(dev->hcd, transfer);
    
    for (int i = 0; i < 10000000 && transfer->status == 0; i++) {
        asm volatile("pause");
    }
    
    ret = transfer->status;
    free(transfer);
    
    return ret;
}

// ==================== DESCRIPTORS ====================

int usb_get_descriptor(usb_device_t *dev, u8 type, u8 index, void *buf, u16 len) {
    return usb_control_transfer(dev, 
        USB_DIR_IN | USB_RECIP_DEVICE, USB_REQ_GET_DESCRIPTOR,
        (type << 8) | index, 0, len, buf);
}

int usb_get_device_descriptor(usb_device_t *dev, usb_device_descriptor_t *desc) {
    int ret = usb_get_descriptor(dev, USB_DESC_DEVICE, 0, desc, sizeof(usb_device_descriptor_t));
    if (ret < 0) return ret;
    
    dev->vendor_id = desc->idVendor;
    dev->product_id = desc->idProduct;
    dev->device_class = desc->bDeviceClass;
    dev->device_subclass = desc->bDeviceSubClass;
    dev->device_protocol = desc->bDeviceProtocol;
    dev->max_packet_size0 = desc->bMaxPacketSize0;
    
    return 0;
}

// ==================== ENUMERATION ====================

int usb_set_address(usb_device_t *dev, u8 address) {
    return usb_control_transfer(dev, 
        USB_DIR_OUT | USB_RECIP_DEVICE, USB_REQ_SET_ADDRESS,
        address, 0, 0, NULL);
}

int usb_set_configuration(usb_device_t *dev, u8 configuration) {
    return usb_control_transfer(dev,
        USB_DIR_OUT | USB_RECIP_DEVICE, USB_REQ_SET_CONFIGURATION,
        configuration, 0, 0, NULL);
}

int usb_enumeration(usb_device_t *dev) {
    if (!dev) return -1;
    
    terminal_printf("[USB] Enumerating device...\n");
    
    // Get device descriptor
    usb_device_descriptor_t desc;
    if (usb_get_device_descriptor(dev, &desc) < 0) {
        terminal_error_printf("[USB] Failed to get device descriptor\n");
        return -1;
    }
    
    terminal_printf("[USB] Device: %04X:%04X (%s)\n",
                   desc.idVendor, desc.idProduct,
                   usb_class_string(desc.bDeviceClass));
    
    // Set address
    u8 new_address = dev->address;
    if (usb_set_address(dev, new_address) < 0) {
        terminal_error_printf("[USB] Failed to set address\n");
        return -1;
    }
    
    // Get full config descriptor
    u8 config_buf[256];
    if (usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, config_buf, 256) < 0) {
        terminal_error_printf("[USB] Failed to get config descriptor\n");
        return -1;
    }
    
    usb_config_descriptor_t *config = (usb_config_descriptor_t*)config_buf;
    
    // Set configuration
    if (usb_set_configuration(dev, config->bConfigurationValue) < 0) {
        terminal_error_printf("[USB] Failed to set configuration\n");
        return -1;
    }
    
    terminal_printf("[USB] Device enumerated: address %d\n", dev->address);
    
    return 0;
}