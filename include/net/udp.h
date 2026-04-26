#ifndef NET_UDP_H
#define NET_UDP_H

#include <net/net.h>
#include <net/ipv4.h>

#define UDP_HEADER_SIZE 8
#define UDP_MAX_PAYLOAD 65507

typedef struct __attribute__((packed)) udp_header {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} udp_header_t;

/*
 * UDP handler type
 */
typedef void (*udp_handler_t)(net_buf_t *buf, ipv4_addr_t src, ipv4_addr_t dst,
                               u16 src_port, u16 dst_port);

/*
 * Registering a handler for a port
 */
int udp_register_handler(u16 port, udp_handler_t handler);
void udp_unregister_handler(u16 port);

/*
 * Basic functions
 */
void udp_init(void);
int udp_input(net_buf_t *buf, ipv4_addr_t src, ipv4_addr_t dst);
int udp_output(net_buf_t *buf, ipv4_addr_t dest, ipv4_addr_t src,
               u16 dest_port, u16 src_port);

#endif
