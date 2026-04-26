#ifndef NET_IPV6_H
#define NET_IPV6_H

#include <net/net.h>

/*
 * ============================================================================== IPv6 constants ==================================================================================
 */

#define IPV6_ADDR_LEN       16
#define IPV6_HEADER_SIZE    40

/*
 * Next Header meanings
 */
#define IPV6_NEXT_HOP_TCP   6
#define IPV6_NEXT_HOP_UDP   17
#define IPV6_NEXT_HOP_ICMP  58
#define IPV6_NEXT_HOP_HOP   0
#define IPV6_NEXT_HOP_ROUTE 43
#define IPV6_NEXT_HOP_FRAG  44
#define IPV6_NEXT_HOP_ESP   50
#define IPV6_NEXT_HOP_AH    51
#define IPV6_NEXT_HOP_NONE  59
#define IPV6_NEXT_HOP_DEST  60

/*
 * ============================================================================== IPv6 header ==================================================================================
 */

typedef struct __attribute__((packed)) ipv6_header {
    u32 version:4;      /*
 * Version = 6
 */
    u32 traffic_class:8;
    u32 flow_label:20;
    u16 payload_len;    /*
 * Payload length (without header)
 */
    u8  next_header;    /*
 * Next header
 */
    u8  hop_limit;      /*
 * Hop limit
 */
    u8  src_addr[16];   /*
 * Source address
 */
    u8  dst_addr[16];   /*
 * Destination address
 */
} ipv6_header_t;

/*
 * =============================================================================== IPv6 address ======================================================================================
 */

typedef struct {
    union {
        u8 octets[16];
        u16 words[8];
        u32 dwords[4];
    };
} ipv6_addr_t;

/*
 * Constant addresses
 */
extern const ipv6_addr_t IPV6_ADDR_UNSPECIFIED;    /*
 * ::
 */
extern const ipv6_addr_t IPV6_ADDR_LOOPBACK;       /*
 * ::1
 */
extern const ipv6_addr_t IPV6_ADDR_ALL_NODES;      /*
 * ff02::1
 */
extern const ipv6_addr_t IPV6_ADDR_ALL_ROUTERS;    /*
 * ff02::2
 */

/*
 * Conversion
 */
ipv6_addr_t ipv6_pton(const char *str);
void ipv6_ntop(ipv6_addr_t addr, char *buf, u32 size);

/*
 * Checks
 */
bool ipv6_addr_is_multicast(ipv6_addr_t addr);
bool ipv6_addr_is_link_local(ipv6_addr_t addr);
bool ipv6_addr_is_loopback(ipv6_addr_t addr);
bool ipv6_addr_is_unspecified(ipv6_addr_t addr);

/*
 * ============================================================================== Main functions ======================================================================================
 */

/*
 * IPv6 initialization
 */
void ipv6_init(void);

/*
 * Sending an IPv6 packet
 */
int ipv6_output(net_buf_t *buf, ipv6_addr_t *dest, ipv6_addr_t *src, u8 next_header);

/*
 * Processing an incoming IPv6 packet
 */
void ipv6_input(net_buf_t *buf, net_device_t *dev);

/*
 * Registering Upper Level Protocols
 */
int ipv6_register_protocol(u8 next_header, net_rx_handler_t handler);
void ipv6_unregister_protocol(u8 next_header);

#endif /*
 * NET_IPV6_H
 */
