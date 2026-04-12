#include <net/net.h>
#include <net/ethernet.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <net/ipv4.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <net/udp.h>

/*
 * ============================================================================== Global Variables ======================================================================================
 */

list_head_t net_devices;
static u32 net_device_count = 0;

/*
 * =============================================================================== Buffers Management =====================================================================================
 */

net_buf_t *net_alloc_buf(u32 size) {
    net_buf_t *buf = (net_buf_t*)malloc(sizeof(net_buf_t));
    if (!buf) return NULL;
    
    u32 total_size = size + NET_HEADROOM;
    buf->head = (u8*)malloc(total_size);
    if (!buf->head) {
        free(buf);
        return NULL;
    }
    
    buf->data = buf->head + NET_HEADROOM;
    buf->tail = buf->data;
    buf->end = buf->head + total_size;
    buf->len = 0;
    buf->headroom = NET_HEADROOM;
    buf->truesize = total_size;
    buf->protocol = 0;
    buf->ifindex = 0;
    buf->dev = NULL;
    buf->next = NULL;
    
    return buf;
}

void net_free_buf(net_buf_t *buf) {
    if (!buf) return;
    if (buf->head) free(buf->head);
    free(buf);
}

void net_reserve(net_buf_t *buf, u32 headroom) {
    if (!buf || headroom > buf->headroom) return;
    buf->data += headroom;
    buf->headroom -= headroom;
}

void net_put(net_buf_t *buf, u32 len) {
    if (!buf || buf->tail + len > buf->end) return;
    buf->tail += len;
    buf->len += len;
}

void net_pull(net_buf_t *buf, u32 len) {
    if (!buf || len > buf->len) return;
    buf->data += len;
    buf->len -= len;
}

/*
 * =============================================================================== Device Management ==================================================================================== Device Management
 */

int net_register_device(net_device_t *dev) {
    if (!dev) return -1;
    
    dev->index = net_device_count++;
    list_add_tail(&net_devices, &dev->node);
    
    terminal_printf("[NET] Registered device %s (index=%d, MAC=", dev->name, dev->index);
    char mac_str[18];
    ethernet_mac_ntop(dev->mac_addr, mac_str, 18);
    terminal_printf("%s)\n", mac_str);
    
    return 0;
}

int net_unregister_device(net_device_t *dev) {
    if (!dev) return -1;
    
    list_del(&dev->node);
    net_device_count--;
    
    return 0;
}

int net_rx(net_buf_t *buf, net_device_t *dev) {
    if (!buf || !dev) return -1;
    
    buf->dev = dev;
    ethernet_input(buf, dev);
    
    return 0;
}

int net_tx(net_buf_t *buf, net_device_t *dev) {
    if (!buf || !dev) return -1;
    if (!dev->xmit) return -1;
    
    return dev->xmit(buf, dev);
}

net_device_t *net_get_device(u32 index) {
    list_head_t *pos;
    
    list_for_each(pos, &net_devices) {
        net_device_t *dev = list_entry(pos, net_device_t, node);
        if (dev->index == index) return dev;
    }
    
    return NULL;
}

net_device_t *net_get_device_by_name(const char *name) {
    list_head_t *pos;
    
    list_for_each(pos, &net_devices) {
        net_device_t *dev = list_entry(pos, net_device_t, node);
        if (strcmp(dev->name, name) == 0) return dev;
    }
    
    return NULL;
}

net_device_t *net_get_first_device(void) {
    if (list_empty(&net_devices)) return NULL;
    return list_entry(net_devices.next, net_device_t, node);
}

net_device_t *net_get_first_active(void) {
    list_head_t *pos;
    
    list_for_each(pos, &net_devices) {
        net_device_t *dev = list_entry(pos, net_device_t, node);
        if (dev->up && dev->running) {
            return dev;
        }
    }
    
    return NULL;
}

int net_get_devices_by_type(net_device_type_t type, net_device_t **devices, int max) {
    list_head_t *pos;
    int count = 0;
    
    if (!devices || max <= 0) return 0;
    
    list_for_each(pos, &net_devices) {
        net_device_t *dev = list_entry(pos, net_device_t, node);
        if (dev->type == type) {
            devices[count++] = dev;
            if (count >= max) break;
        }
    }
    
    return count;
}

/*
 * ============================================================================== Network Initialization ===================================================================================
 */

void net_init(void) {
    list_init(&net_devices);
    net_device_count = 0;
    
    terminal_printf("\n=== Network Stack Initialization ===\n");
    
    ethernet_init();
    arp_init();
    ipv4_init();
    ipv6_init();
    icmp_init();
    tcp_init();
    udp_init();
    
    terminal_printf("====================================\n\n");
}
