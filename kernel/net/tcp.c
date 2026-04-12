#include <net/tcp.h>
#include <net/ipv4.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <mm/heap.h>
#include <net/ethernet.h>

/*
 * Global tables
 */
static list_head_t tcp_sockets;
static u32 tcp_iss = 0x12345678;

/*
 * ============================================================================= TCP Checksum ============================================================================
 */

u16 tcp_checksum(tcp_header_t *tcp, u32 len, ipv4_addr_t src, ipv4_addr_t dst) {
    struct {
        u32 src_addr;
        u32 dst_addr;
        u8 zero;
        u8 protocol;
        u16 tcp_len;
    } __attribute__((packed)) pseudo;
    
    u32 sum = 0;
    u16 *ptr;
    u32 i;
    
    pseudo.src_addr = src.addr;
    pseudo.dst_addr = dst.addr;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_TCP;
    pseudo.tcp_len = htons(len);
    
    ptr = (u16*)&pseudo;
    for (i = 0; i < sizeof(pseudo) / 2; i++) {
        sum += ptr[i];
    }
    
    ptr = (u16*)tcp;
    for (i = 0; i < len / 2; i++) {
        sum += ptr[i];
    }
    
    if (len & 1) {
        sum += *(u8*)((u8*)tcp + len - 1);
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/*
 * =============================================================================== Creating/deleting a socket ===============================================================================
 */

tcp_socket_t *tcp_socket_create(void) {
    tcp_socket_t *sock = (tcp_socket_t*)malloc(sizeof(tcp_socket_t));
    if (!sock) return NULL;
    
    memset(sock, 0, sizeof(tcp_socket_t));
    sock->state = TCP_STATE_CLOSED;
    sock->rcv_wnd = 65535;
    sock->snd_wnd = 65535;
    sock->mss = 1460;
    
    sock->rx_queue_head = NULL;
    sock->rx_queue_tail = NULL;
    sock->rx_queue_count = 0;
    sock->tx_queue_head = NULL;
    sock->tx_queue_tail = NULL;
    sock->tx_queue_count = 0;
    sock->accept_queue_head = NULL;
    sock->accept_queue_tail = NULL;
    sock->accept_queue_count = 0;
    sock->backlog = 0;
    
    return sock;
}

void tcp_socket_destroy(tcp_socket_t *sock) {
    net_buf_t *buf;
    
    if (!sock) return;
    
    while (sock->rx_queue_head) {
        buf = sock->rx_queue_head;
        sock->rx_queue_head = buf->next;
        net_free_buf(buf);
    }
    
    while (sock->tx_queue_head) {
        buf = sock->tx_queue_head;
        sock->tx_queue_head = buf->next;
        net_free_buf(buf);
    }
    
    while (sock->accept_queue_head) {
        buf = sock->accept_queue_head;
        sock->accept_queue_head = buf->next;
        net_free_buf(buf);
    }
    
    free(sock);
}

/*
 * ============================================================================== TCP segment sending ====================================================================================
 */

static int tcp_send_segment(tcp_socket_t *sock, u32 seq, u32 ack, u16 flags,
                            const u8 *data, u32 data_len) {
    net_buf_t *buf = net_alloc_buf(TCP_HEADER_MAX + data_len);
    if (!buf) return -1;
    
    net_reserve(buf, IPV4_HEADER_MIN + ETH_HLEN);
    
    tcp_header_t *tcp = (tcp_header_t*)buf->data;
    memset(tcp, 0, sizeof(tcp_header_t));
    
    tcp->src_port = htons(sock->local_port);
    tcp->dst_port = htons(sock->remote_port);
    tcp->seq_num = htonl(seq);
    tcp->ack_num = htonl(ack);
    TCP_SET_DATA_OFFSET(tcp, 5);
    TCP_SET_FLAGS(tcp, flags);
    tcp->window = htons(sock->rcv_wnd);
    
    if (data && data_len) {
        u8 *payload = (u8*)tcp + TCP_HEADER_MIN;
        memcpy(payload, data, data_len);
        buf->len = TCP_HEADER_MIN + data_len;
    } else {
        buf->len = TCP_HEADER_MIN;
    }
    
    tcp->checksum = 0;
    tcp->checksum = tcp_checksum(tcp, buf->len, sock->local_addr, sock->remote_addr);
    
    return ipv4_output(buf, sock->remote_addr, sock->local_addr, IP_PROTO_TCP);
}

/*
 * ============================================================================= TCP listen/accept ============================================================================
 */

int tcp_listen(tcp_socket_t *sock, int backlog) {
    if (!sock || sock->state != TCP_STATE_CLOSED) return -1;
    
    sock->state = TCP_STATE_LISTEN;
    sock->backlog = backlog;
    return 0;
}

tcp_socket_t *tcp_accept(tcp_socket_t *listen_sock) {
    tcp_socket_t *new_sock = NULL;
    net_buf_t *buf;
    
    if (!listen_sock || listen_sock->state != TCP_STATE_LISTEN) return NULL;
    if (!listen_sock->accept_queue_head) return NULL;
    
    buf = listen_sock->accept_queue_head;
    listen_sock->accept_queue_head = buf->next;
    if (!listen_sock->accept_queue_head) {
        listen_sock->accept_queue_tail = NULL;
    }
    listen_sock->accept_queue_count--;
    
    new_sock = *(tcp_socket_t**)buf->data;
    net_free_buf(buf);
    
    return new_sock;
}

/*
 * ============================================================================= TCP connect ============================================================================
 */

int tcp_connect(tcp_socket_t *sock, ipv4_addr_t addr, u16 port) {
    if (!sock || sock->state != TCP_STATE_CLOSED) return -1;
    
    sock->remote_addr = addr;
    sock->remote_port = port;
    sock->iss = tcp_iss;
    tcp_iss += 1000;
    
    sock->snd_una = sock->iss;
    sock->snd_nxt = sock->iss + 1;
    sock->rcv_nxt = 0;
    
    sock->state = TCP_STATE_SYN_SENT;
    
    return tcp_send_segment(sock, sock->iss, 0, TCP_FLAG_SYN, NULL, 0);
}

/*
 * ============================================================================= TCP send/recv ============================================================================
 */

int tcp_send(tcp_socket_t *sock, const u8 *data, u32 len) {
    if (!sock || sock->state != TCP_STATE_ESTABLISHED) return -1;
    if (!data || len == 0) return -1;
    
    u32 remaining = len;
    u32 offset = 0;
    
    while (remaining > 0) {
        u32 chunk = remaining;
        if (chunk > sock->mss) chunk = sock->mss;
        
        if (tcp_send_segment(sock, sock->snd_nxt, sock->rcv_nxt,
                             TCP_FLAG_ACK | TCP_FLAG_PSH,
                             data + offset, chunk) != 0) {
            return -1;
        }
        
        sock->snd_nxt += chunk;
        offset += chunk;
        remaining -= chunk;
    }
    
    return len;
}

int tcp_recv(tcp_socket_t *sock, u8 *buf, u32 len) {
    net_buf_t *data_buf;
    u32 copy_len;
    
    if (!sock || !buf) return -1;
    if (sock->state != TCP_STATE_ESTABLISHED) return -1;
    if (!sock->rx_queue_head) return 0;
    
    data_buf = sock->rx_queue_head;
    copy_len = data_buf->len;
    if (copy_len > len) copy_len = len;
    
    memcpy(buf, data_buf->data, copy_len);
    
    sock->rx_queue_head = data_buf->next;
    if (!sock->rx_queue_head) {
        sock->rx_queue_tail = NULL;
    }
    sock->rx_queue_count--;
    
    net_free_buf(data_buf);
    
    return copy_len;
}

/*
 * ============================================================================= TCP close ============================================================================
 */

int tcp_close(tcp_socket_t *sock) {
    if (!sock) return -1;
    
    switch (sock->state) {
        case TCP_STATE_ESTABLISHED:
            sock->state = TCP_STATE_FIN_WAIT_1;
            tcp_send_segment(sock, sock->snd_nxt, sock->rcv_nxt,
                             TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            sock->snd_nxt++;
            break;
            
        case TCP_STATE_CLOSE_WAIT:
            sock->state = TCP_STATE_LAST_ACK;
            tcp_send_segment(sock, sock->snd_nxt, sock->rcv_nxt,
                             TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            sock->snd_nxt++;
            break;
            
        default:
            tcp_socket_destroy(sock);
            break;
    }
    
    return 0;
}

int tcp_abort(tcp_socket_t *sock) {
    if (!sock) return -1;
    
    tcp_send_segment(sock, sock->snd_nxt, sock->rcv_nxt, TCP_FLAG_RST, NULL, 0);
    tcp_socket_destroy(sock);
    
    return 0;
}

/*
 * ============================================================================== Processing incoming segments =================================================================================
 */

static int tcp_process(net_buf_t *buf, ipv4_addr_t src, ipv4_addr_t dst,
                       u16 src_port, u16 dst_port) {
    tcp_header_t *tcp = (tcp_header_t*)buf->data;
    u32 data_offset = TCP_GET_DATA_OFFSET(tcp) * 4;
    u32 seq = ntohl(tcp->seq_num);
    u32 ack = ntohl(tcp->ack_num);
    u16 flags = TCP_GET_FLAGS(tcp);
    u32 data_len = buf->len - data_offset;
    
    tcp_socket_t *sock = NULL;
    
    /*
 * TODO: normal socket search by port
 */
    /*
 * Just a stub for now
 */
    
    if (data_offset > buf->len) return -1;
    
    net_pull(buf, data_offset);
    
    if (!sock) return -1;
    
    switch (sock->state) {
        case TCP_STATE_LISTEN:
            if (flags & TCP_FLAG_SYN) {
                tcp_socket_t *new_sock = tcp_socket_create();
                if (!new_sock) break;
                
                new_sock->local_addr = dst;
                new_sock->local_port = dst_port;
                new_sock->remote_addr = src;
                new_sock->remote_port = src_port;
                
                new_sock->irs = seq;
                new_sock->rcv_nxt = seq + 1;
                new_sock->iss = tcp_iss;
                tcp_iss += 1000;
                new_sock->snd_una = new_sock->iss;
                new_sock->snd_nxt = new_sock->iss + 1;
                
                new_sock->state = TCP_STATE_SYN_RCVD;
                
                tcp_send_segment(new_sock, new_sock->iss, new_sock->rcv_nxt,
                                 TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                
                if (sock->backlog == 0 || sock->accept_queue_count < sock->backlog) {
                    net_buf_t *pending = net_alloc_buf(sizeof(tcp_socket_t*));
                    if (pending) {
                        *(tcp_socket_t**)pending->data = new_sock;
                        pending->len = sizeof(tcp_socket_t*);
                        pending->next = NULL;
                        
                        if (sock->accept_queue_tail) {
                            sock->accept_queue_tail->next = pending;
                            sock->accept_queue_tail = pending;
                        } else {
                            sock->accept_queue_head = pending;
                            sock->accept_queue_tail = pending;
                        }
                        sock->accept_queue_count++;
                    } else {
                        tcp_socket_destroy(new_sock);
                    }
                } else {
                    tcp_send_segment(new_sock, 0, new_sock->rcv_nxt,
                                     TCP_FLAG_RST | TCP_FLAG_ACK, NULL, 0);
                    tcp_socket_destroy(new_sock);
                }
            }
            break;
            
        case TCP_STATE_ESTABLISHED:
            if (flags & TCP_FLAG_ACK) {
                if (ack > sock->snd_una && ack <= sock->snd_nxt) {
                    sock->snd_una = ack;
                }
            }
            
            if (data_len > 0 && (flags & TCP_FLAG_ACK)) {
                net_buf_t *data_buf = net_alloc_buf(data_len);
                if (data_buf) {
                    memcpy(data_buf->data, buf->data, data_len);
                    data_buf->len = data_len;
                    data_buf->next = NULL;
                    
                    if (sock->rx_queue_tail) {
                        sock->rx_queue_tail->next = data_buf;
                        sock->rx_queue_tail = data_buf;
                    } else {
                        sock->rx_queue_head = data_buf;
                        sock->rx_queue_tail = data_buf;
                    }
                    sock->rx_queue_count++;
                    sock->rcv_nxt += data_len;
                }
            }
            
            tcp_send_segment(sock, sock->snd_nxt, sock->rcv_nxt,
                             TCP_FLAG_ACK, NULL, 0);
            break;
            
        default:
            break;
    }
    
    return 0;
}

static void tcp_input_wrapper(net_buf_t *buf, net_device_t *dev) {
    ipv4_header_t *iph;
    tcp_header_t *tcp;
    
    (void)dev;
    
    if (buf->len < sizeof(tcp_header_t)) return;
    
    iph = (ipv4_header_t*)(buf->data - sizeof(ipv4_header_t));
    tcp = (tcp_header_t*)buf->data;
    
    ipv4_addr_t src = { .addr = iph->src_addr };
    ipv4_addr_t dst = { .addr = iph->dst_addr };
    
    tcp_process(buf, src, dst, ntohs(tcp->src_port), ntohs(tcp->dst_port));
}

/*
 * =============================================================================== Initialization ======================================================================================
 */

void tcp_init(void) {
    list_init(&tcp_sockets);
    
    ipv4_register_protocol(IP_PROTO_TCP, tcp_input_wrapper);
    
    terminal_printf("[TCP] Initialized\n");
}
