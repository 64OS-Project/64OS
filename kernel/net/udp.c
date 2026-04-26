#include <net/udp.h>
#include <libk/string.h>
#include <kernel/terminal.h>

static udp_handler_t udp_handlers[65536];

/*
 * ============================================================================== Registration of handlers ================================================================================
 */

int udp_register_handler(u16 port, udp_handler_t handler) {
    if (udp_handlers[port]) return -1;
    udp_handlers[port] = handler;
    terminal_printf("[UDP] Registered handler for port %d\n", port);
    return 0;
}

void udp_unregister_handler(u16 port) {
    udp_handlers[port] = NULL;
}

/*
 * ============================================================================== Incoming UDP processing =================================================================================
 */

int udp_input(net_buf_t *buf, ipv4_addr_t src, ipv4_addr_t dst) {
    if (buf->len < UDP_HEADER_SIZE) return -1;
    
    udp_header_t *udp = (udp_header_t*)buf->data;
    u16 src_port = ntohs(udp->src_port);
    u16 dst_port = ntohs(udp->dst_port);
    u16 len = ntohs(udp->length);
    
    if (len > buf->len) return -1;
    
    /*
 * TODO: checksum check
 */
    
    /*
 * Removing the UDP header
 */
    net_pull(buf, UDP_HEADER_SIZE);
    
    /*
 * Call the handler
 */
    if (udp_handlers[dst_port]) {
        udp_handlers[dst_port](buf, src, dst, src_port, dst_port);
    }
    
    return 0;
}

int udp_output(net_buf_t *buf, ipv4_addr_t dest, ipv4_addr_t src,
               u16 dest_port, u16 src_port) {
    net_reserve(buf, UDP_HEADER_SIZE);
    
    udp_header_t *udp = (udp_header_t*)buf->data;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dest_port);
    udp->length = htons(buf->len + UDP_HEADER_SIZE);
    udp->checksum = 0;  /*
 * TODO: add checksum
 */
    
    buf->len += UDP_HEADER_SIZE;
    
    return ipv4_output(buf, dest, src, IP_PROTO_UDP);
}

void udp_init(void) {
    memset(udp_handlers, 0, sizeof(udp_handlers));
    ipv4_register_protocol(IP_PROTO_UDP, (net_rx_handler_t)udp_input);
    terminal_printf("[UDP] Initialized\n");
}
