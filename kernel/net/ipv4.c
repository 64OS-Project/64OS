#include <net/ipv4.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <net/ethernet.h>
#include <mm/heap.h>

/*
 * Global routing table
 */
static list_head_t ipv4_routes;
static net_rx_handler_t ipv4_protocols[256];  /*
 * Upper level protocols
 */
static u16 ipv4_id_counter = 0;

/*
 * ============================================================================== IPv4 addressing functions ==================================================================================
 */

ipv4_addr_t ipv4_pton(const char *str) {
    ipv4_addr_t addr = { .addr = 0 };
    u32 octets[4] = {0, 0, 0, 0};
    int i = 0;
    u32 val = 0;
    
    while (*str && i < 4) {
        if (*str >= '0' && *str <= '9') {
            val = val * 10 + (*str - '0');
        } else if (*str == '.') {
            octets[i++] = val;
            val = 0;
        } else {
            break;
        }
        str++;
    }
    if (i == 3 && val <= 255) {
        octets[3] = val;
        addr.octets[0] = octets[0];
        addr.octets[1] = octets[1];
        addr.octets[2] = octets[2];
        addr.octets[3] = octets[3];
    }
    
    return addr;
}

void ipv4_ntop(ipv4_addr_t addr, char *buf, u32 size) {
    snprintf(buf, size, "%u.%u.%u.%u",
             addr.octets[0], addr.octets[1],
             addr.octets[2], addr.octets[3]);
}

bool ipv4_addr_is_multicast(ipv4_addr_t addr) {
    return (addr.octets[0] & 0xF0) == 0xE0;  /*
 * 224.0.0.0 - 239.255.255.255
 */
}

bool ipv4_addr_is_broadcast(ipv4_addr_t addr) {
    return addr.addr == 0xFFFFFFFF;
}

bool ipv4_addr_is_loopback(ipv4_addr_t addr) {
    return (addr.octets[0] == 127);
}

/*
 * ============================================================================= IPv4 Checksum (RFC 1071) ============================================================================
 */

u16 ipv4_checksum(u16 *data, u32 len) {
    u32 sum = 0;
    
    while (len > 1) {
        sum += *data++;
        len -= 2;
    }
    
    if (len) {
        sum += *(u8*)data;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/*
 * =============================================================================== Routing ====================================================================================
 */

int ipv4_route_add(ipv4_addr_t dest, ipv4_addr_t netmask,
                   ipv4_addr_t gateway, ipv4_addr_t src,
                   net_device_t *dev, u32 metric) {
    ipv4_route_t *route = (ipv4_route_t*)malloc(sizeof(ipv4_route_t));
    if (!route) return -1;
    
    route->dest = dest;
    route->netmask = netmask;
    route->gateway = gateway;
    route->src = src;
    route->dev = dev;
    route->metric = metric;
    
    list_add_tail(&ipv4_routes, &route->node);
    
    terminal_printf("[IPv4] Route added: ");
    char buf[16];
    ipv4_ntop(dest, buf, 16);
    terminal_printf("%s/", buf);
    ipv4_ntop(netmask, buf, 16);
    terminal_printf("%s via ", buf);
    ipv4_ntop(gateway, buf, 16);
    terminal_printf("%s on %s\n", buf, dev->name);
    
    return 0;
}

int ipv4_route_del(ipv4_addr_t dest, ipv4_addr_t netmask) {
    list_head_t *pos, *n;
    
    list_for_each_safe(pos, n, &ipv4_routes) {
        ipv4_route_t *route = list_entry(pos, ipv4_route_t, node);
        
        if (route->dest.addr == dest.addr && route->netmask.addr == netmask.addr) {
            list_del(&route->node);
            free(route);
            return 0;
        }
    }
    
    return -1;
}

ipv4_route_t *ipv4_route_lookup(ipv4_addr_t dest) {
    ipv4_route_t *best = NULL;
    list_head_t *pos;
    
    list_for_each(pos, &ipv4_routes) {
        ipv4_route_t *route = list_entry(pos, ipv4_route_t, node);
        u32 masked_dest = dest.addr & route->netmask.addr;
        
        if (masked_dest == route->dest.addr) {
            if (!best || route->metric < best->metric) {
                best = route;
            }
        }
    }
    
    return best;
}

ipv4_route_t *ipv4_route_get_default(void) {
    list_head_t *pos;
    
    list_for_each(pos, &ipv4_routes) {
        ipv4_route_t *route = list_entry(pos, ipv4_route_t, node);
        if (route->dest.addr == 0 && route->netmask.addr == 0) {
            return route;
        }
    }
    
    return NULL;
}

/*
 * ============================================================================= Sending an IPv4 packet =================================================================================
 */

int ipv4_output(net_buf_t *buf, ipv4_addr_t dest, ipv4_addr_t src, u8 protocol) {
    ipv4_route_t *route = ipv4_route_lookup(dest);
    if (!route) {
        route = ipv4_route_get_default();
        if (!route) return -1;
    }
    
    /*
 * Reserve space for the title
 */
    net_reserve(buf, IPV4_HEADER_MIN);
    
    ipv4_header_t *iph = (ipv4_header_t*)buf->data;
    memset(iph, 0, sizeof(ipv4_header_t));
    
    iph->version = 4;
    iph->ihl = 5;  /*
 * 5 4 = 20 bytes
 */
    iph->tos = 0;
    iph->total_len = htons(buf->len);
    iph->id = htons(++ipv4_id_counter);
    iph->frag_off = 0;
    iph->ttl = 64;
    iph->protocol = protocol;
    iph->src_addr = src.addr;
    
    /*
 * If src is not specified, take it from the route or interface
 */
    if (iph->src_addr == 0) {
        if (route->src.addr != 0) {
            iph->src_addr = route->src.addr;
        } else if (route->dev) {
            iph->src_addr = ((ipv4_addr_t*)route->dev->ip_addr)->addr;
        } else {
            return -1;
        }
    }
    
    iph->dst_addr = dest.addr;
    
    /*
 * Calculating checksum
 */
    iph->checksum = 0;
    iph->checksum = ipv4_checksum((u16*)iph, iph->ihl * 4);
    
    /*
 * Determining next hop
 */
    ipv4_addr_t next_hop = dest;
    if (route->gateway.addr != 0) {
        next_hop = route->gateway;
    }
    
    /*
 * Send via ARP (if not loopback)
 */
    if (!ipv4_addr_is_loopback(dest)) {
        u8 dst_mac[ETH_ALEN];
        if (arp_resolve(route->dev, next_hop, dst_mac, 1000) == 0) {
            return ethernet_output(buf, route->dev, dst_mac, ETH_P_IP);
        }
        return -1;
    } else {
        /*
 * Loopback - send to ourselves
 */
        return ethernet_output(buf, route->dev, route->dev->mac_addr, ETH_P_IP);
    }
    
    return net_tx(buf, route->dev);
}

/*
 * ============================================================================== Processing of incoming IPv4 packet ================================================================================
 */

void ipv4_input(net_buf_t *buf, net_device_t *dev) {
    ipv4_header_t *iph = (ipv4_header_t*)buf->data;
    
    /*
 * Version check
 */
    if (iph->version != 4) {
        terminal_error_printf("[IPv4] Invalid version %d\n", iph->version);
        return;
    }
    
    /*
 * Length check
 */
    if (buf->len < IPV4_HEADER_MIN) {
        terminal_error_printf("[IPv4] Packet too short\n");
        return;
    }
    
    /*
 * Checksum check
 */
    u16 checksum = iph->checksum;
    iph->checksum = 0;
    if (ipv4_checksum((u16*)iph, iph->ihl * 4) != checksum) {
        terminal_error_printf("[IPv4] Bad checksum\n");
        return;
    }
    iph->checksum = checksum;
    
    /*
 * TTL
 */
    if (iph->ttl <= 1) {
        /*
 * Forming ICMP Time Exceeded
 */
        net_buf_t *icmp_buf = net_alloc_buf(buf->len + 8);
        if (icmp_buf) {
            /*
 * Copy the original IP header + 8 bytes of data
 */
            memcpy(icmp_buf->data, iph, iph->ihl * 4 + 8);
            icmp_buf->len = iph->ihl * 4 + 8;
        
            icmp_output(icmp_buf, (ipv4_addr_t){ .addr = iph->src_addr },
                    (ipv4_addr_t){ .addr = iph->dst_addr },
                    ICMP_TYPE_TIME_EXCEEDED, ICMP_CODE_TTL_EXCEEDED);
            net_free_buf(icmp_buf);
        }
        return;
    }
    
    /*
 * Removing the title
 */
    net_pull(buf, iph->ihl * 4);
    
    /*
 * We pass it to the upper level protocol
 */
    if (iph->protocol < 256 && ipv4_protocols[iph->protocol]) {
        ipv4_protocols[iph->protocol](buf, dev);
    } else {
        terminal_warn_printf("[IPv4] Unsupported protocol %d\n", iph->protocol);
    }
    
    return;
}

/*
 * ============================================================================== Registering protocols ====================================================================================
 */

int ipv4_register_protocol(u8 protocol, net_rx_handler_t handler) {
    if (protocol >= 256) return -1;
    if (ipv4_protocols[protocol]) return -1;
    
    ipv4_protocols[protocol] = handler;
    terminal_printf("[IPv4] Registered protocol %d\n", protocol);
    return 0;
}

void ipv4_unregister_protocol(u8 protocol) {
    if (protocol < 256) {
        ipv4_protocols[protocol] = NULL;
    }
}

/*
 * =============================================================================== Initialization ======================================================================================
 */

void ipv4_init(void) {
    list_init(&ipv4_routes);
    memset(ipv4_protocols, 0, sizeof(ipv4_protocols));
    
    terminal_printf("[IPv4] Initialized\n");
}
