#ifndef NET_IPV4_H
#define NET_IPV4_H

#include <net/net.h>

/*
 * ============================================================================== IPv4 constants ==================================================================================
 */

#define IPV4_ADDR_LEN       4
#define IPV4_HEADER_MIN     20
#define IPV4_HEADER_MAX     60

/*
 * Protocols
 */
#define IPV4_PROTO_ICMP     1
#define IPV4_PROTO_TCP      6
#define IPV4_PROTO_UDP      17

/*
 * Fragmentation Flags
 */
#define IPV4_FLAG_MORE_FRAG  0x2000
#define IPV4_FLAG_DONT_FRAG  0x4000
#define IPV4_FLAG_RESERVED   0x8000
#define IPV4_FRAG_OFFSET_MASK 0x1FFF

/*
 * ============================================================================== IPv4 header ==================================================================================
 */

typedef struct __attribute__((packed)) ipv4_header {
    u8  ihl:4;          /*
 * Internet Header Length (in 32-bit words)
 */
    u8  version:4;      /*
 * Version = 4
 */
    u8  tos;            /*
 * Type of Service
 */
    u16 total_len;      /*
 * Total length (including header)
 */
    u16 id;             /*
 * Identification
 */
    u16 frag_off;       /*
 * Fragment offset + flags
 */
    u8  ttl;            /*
 * Time To Live
 */
    u8  protocol;       /*
 * Protocol (TCP=6, UDP=17)
 */
    u16 checksum;       /*
 * Header checksum
 */
    u32 src_addr;       /*
 * Source address (network byte order)
 */
    u32 dst_addr;       /*
 * Destination address
 */
} ipv4_header_t;

/*
 * =============================================================================== IPv4 address =====================================================================================
 */

typedef struct {
    union {
        u32 addr;
        u8 octets[4];
    };
} ipv4_addr_t;

/*
 * Conversion
 */
ipv4_addr_t ipv4_pton(const char *str);
void ipv4_ntop(ipv4_addr_t addr, char *buf, u32 size);

/*
 * Checks
 */
bool ipv4_addr_is_multicast(ipv4_addr_t addr);
bool ipv4_addr_is_broadcast(ipv4_addr_t addr);
bool ipv4_addr_is_loopback(ipv4_addr_t addr);

/*
 * =============================================================================== Routing ====================================================================================
 */

typedef struct ipv4_route {
    ipv4_addr_t dest;
    ipv4_addr_t netmask;
    ipv4_addr_t gateway;
    ipv4_addr_t src;
    net_device_t *dev;
    u32 metric;
    
    list_head_t node;
} ipv4_route_t;

/*
 * Adding/removing a route
 */
int ipv4_route_add(ipv4_addr_t dest, ipv4_addr_t netmask,
                   ipv4_addr_t gateway, ipv4_addr_t src,
                   net_device_t *dev, u32 metric);
int ipv4_route_del(ipv4_addr_t dest, ipv4_addr_t netmask);

/*
 * Finding a route
 */
ipv4_route_t *ipv4_route_lookup(ipv4_addr_t dest);
ipv4_route_t *ipv4_route_get_default(void);

/*
 * ============================================================================== Main functions ======================================================================================
 */

/*
 * IPv4 initialization
 */
void ipv4_init(void);

/*
 * Sending an IPv4 packet
 */
int ipv4_output(net_buf_t *buf, ipv4_addr_t dest, ipv4_addr_t src, u8 protocol);

/*
 * Processing an incoming IPv4 packet
 */
void ipv4_input(net_buf_t *buf, net_device_t *dev);

/*
 * Fragmentation
 */
int ipv4_fragment(net_buf_t *buf, u32 mtu);

/*
 * Registering Upper Level Protocols
 */
int ipv4_register_protocol(u8 protocol, net_rx_handler_t handler);
void ipv4_unregister_protocol(u8 protocol);

/*
 * Checksum (RFC 1071)
 */
u16 ipv4_checksum(u16 *data, u32 len);

#endif /*
 * NET_IPV4_H
 */
