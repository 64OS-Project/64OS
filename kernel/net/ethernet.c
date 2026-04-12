#include <net/ethernet.h>
#include <net/arp.h>
#include <net/ipv4.h>
#include <net/ipv6.h>
#include <libk/string.h>
#include <kernel/terminal.h>

const u8 ETH_MAC_BROADCAST[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static net_rx_handler_t eth_protocols[65536];

/*
 * ============================================================================================== MAC address of the function =================================================================================
 */

void ethernet_mac_ntop(u8 *mac, char *buf, u32 size) {
    snprintf(buf, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int ethernet_mac_pton(const char *str, u8 *mac) {
    int i;
    u32 val;
    
    for (i = 0; i < 6; i++) {
        /*
 * Manual hex parsing instead of sscanf
 */
        val = 0;
        if (*str >= '0' && *str <= '9') val = (*str - '0') * 16;
        else if (*str >= 'a' && *str <= 'f') val = (*str - 'a' + 10) * 16;
        else if (*str >= 'A' && *str <= 'F') val = (*str - 'A' + 10) * 16;
        else return -1;
        str++;
        
        if (*str >= '0' && *str <= '9') val += (*str - '0');
        else if (*str >= 'a' && *str <= 'f') val += (*str - 'a' + 10);
        else if (*str >= 'A' && *str <= 'F') val += (*str - 'A' + 10);
        else return -1;
        str++;
        
        mac[i] = val & 0xFF;
        
        if (i < 5 && *str == ':') str++;
    }
    
    return 0;
}

bool ethernet_mac_equal(u8 *mac1, u8 *mac2) {
    return memcmp(mac1, mac2, ETH_ALEN) == 0;
}

/*
 * ============================================================================== Sending/receiving =================================================================================
 */

int ethernet_output(net_buf_t *buf, net_device_t *dev, u8 *dst_mac, u16 type) {
    if (!buf || !dev || !dst_mac) return -1;
    
    net_reserve(buf, ETH_HLEN);
    
    eth_header_t *eth = (eth_header_t*)buf->data;
    memcpy(eth->dst_mac, dst_mac, ETH_ALEN);
    memcpy(eth->src_mac, dev->mac_addr, ETH_ALEN);
    eth->type = htons(type);
    
    /*
 * Add padding to the minimum frame size
 */
    if (buf->len < ETH_MIN_FRAME - ETH_HLEN) {
        u32 pad = ETH_MIN_FRAME - ETH_HLEN - buf->len;
        memset(buf->data + buf->len, 0, pad);
        buf->len += pad;
    }
    
    return dev->xmit(buf, dev);
}

void ethernet_input(net_buf_t *buf, net_device_t *dev) {
    if (buf->len < ETH_HLEN) return;
    
    eth_header_t *eth = (eth_header_t*)buf->data;
    u16 type = ntohs(eth->type);
    
    /*
 * Removing the Ethernet header
 */
    net_pull(buf, ETH_HLEN);
    
    /*
 * We filter not our MAC or broadcast
 */
    if (!ethernet_mac_equal(eth->dst_mac, dev->mac_addr) &&
        !ethernet_mac_equal(eth->dst_mac, (u8*)ETH_MAC_BROADCAST)) {
        return;
    }
    
    /*
 * We pass it to the upper level protocol
 */
    if (type < 65536 && eth_protocols[type]) {
        eth_protocols[type](buf, dev);
    }
}

/*
 * ============================================================================== Registering protocols ====================================================================================
 */

int ethernet_register_protocol(u16 type, net_rx_handler_t handler) {
    if (type >= 65536) return -1;
    if (eth_protocols[type]) return -1;
    
    eth_protocols[type] = handler;
    terminal_printf("[ETH] Registered protocol 0x%04x\n", type);
    return 0;
}

void ethernet_unregister_protocol(u16 type) {
    if (type < 65536) {
        eth_protocols[type] = NULL;
    }
}

/*
 * =============================================================================== Initialization ======================================================================================
 */

void ethernet_init(void) {
    memset(eth_protocols, 0, sizeof(eth_protocols));
    
    /*
 * Registering IP protocols (wrappers for type conversion)
 */
    ethernet_register_protocol(ETH_P_IP, (net_rx_handler_t)ipv4_input);
    ethernet_register_protocol(ETH_P_IPV6, (net_rx_handler_t)ipv6_input);
    ethernet_register_protocol(ETH_P_ARP, (net_rx_handler_t)arp_input);
    
    terminal_printf("[ETH] Initialized\n");
}
