#ifndef NET_TCP_H
#define NET_TCP_H

#include <net/net.h>
#include <net/ipv4.h>

/*
 * ============================================================================= TCP constants ==================================================================================== TCP constants
 */

#define TCP_HEADER_MIN      20
#define TCP_HEADER_MAX      60

/*
 * Flags
 */
#define TCP_FLAG_FIN        0x01
#define TCP_FLAG_SYN        0x02
#define TCP_FLAG_RST        0x04
#define TCP_FLAG_PSH        0x08
#define TCP_FLAG_ACK        0x10
#define TCP_FLAG_URG        0x20
#define TCP_FLAG_ECE        0x40
#define TCP_FLAG_CWR        0x80
#define TCP_FLAG_NS         0x100

/*
 * Socket states
 */
#define TCP_STATE_CLOSED        0
#define TCP_STATE_LISTEN        1
#define TCP_STATE_SYN_SENT      2
#define TCP_STATE_SYN_RCVD      3
#define TCP_STATE_ESTABLISHED   4
#define TCP_STATE_FIN_WAIT_1    5
#define TCP_STATE_FIN_WAIT_2    6
#define TCP_STATE_CLOSE_WAIT    7
#define TCP_STATE_CLOSING       8
#define TCP_STATE_LAST_ACK      9
#define TCP_STATE_TIME_WAIT     10

/*
 * ============================================================================= TCP header =================================================================================== TCP header
 */

typedef struct __attribute__((packed)) tcp_header {
    u16 src_port;
    u16 dst_port;
    u32 seq_num;
    u32 ack_num;
    u16 offset_reserved_flags;  /*
 * Data offset (4) + reserved (3) + flags (9)
 */
    u16 window;
    u16 checksum;
    u16 urgent_ptr;
    /*
 * Options follow the title
 */
} tcp_header_t;

/*
 * Helper macros for flags
 */
#define TCP_GET_DATA_OFFSET(hdr) (((hdr)->offset_reserved_flags >> 12) & 0xF)
#define TCP_GET_FLAGS(hdr) ((hdr)->offset_reserved_flags & 0x1FF)
#define TCP_SET_FLAGS(hdr, flags) ((hdr)->offset_reserved_flags = \
    ((hdr)->offset_reserved_flags & ~0x1FF) | ((flags) & 0x1FF))
#define TCP_SET_DATA_OFFSET(hdr, offset) ((hdr)->offset_reserved_flags = \
    ((hdr)->offset_reserved_flags & ~0xF000) | (((offset) & 0xF) << 12))

/*
 * ============================================================================== TCP socket ==================================================================================== TCP socket
 */

typedef struct tcp_socket {
    /*
 * Five-tuple
 */
    ipv4_addr_t local_addr;
    ipv4_addr_t remote_addr;
    u16 local_port;
    u16 remote_port;
    
    /*
 * State
 */
    u32 state;

    u32 mss;
    
    /*
 * Sequence numbers
 */
    u32 snd_una;        /*
 * Sent but not confirmed
 */
    u32 snd_nxt;        /*
 * Next to send
 */
    u32 snd_wnd;        /*
 * Send window
 */
    u32 snd_up;         /*
 * Urgent pointer
 */
    u32 snd_wl1;        /*
 * Window update segment seq number
 */
    u32 snd_wl2;        /*
 * Window update segment ack number
 */
    u32 iss;            /*
 * Initial send sequence number
 */
    
    u32 rcv_nxt;        /*
 * Next expected to be received
 */
    u32 rcv_wnd;        /*
 * Receive Window
 */
    u32 rcv_up;         /*
 * Urgent pointer
 */
    u32 irs;            /*
 * Initial receive sequence number
 */
    
    /*
 * Timers
 */
    u32 rtt;            /*
 * Round trip time
 */
    u32 rttvar;         /*
 * RTT variance
 */
    u32 rto;            /*
 * Retransmission timeout
 */
    
    /*
 * Windows
 */
    u32 cwnd;           /*
 * Congestion window
 */
    u32 ssthresh;       /*
 * Slow start threshold
 */
    
    /*
 * Queues
 */
    net_buf_t *rx_queue_head;
    net_buf_t *rx_queue_tail;
    u32 rx_queue_count;
    net_buf_t *tx_queue_head;
    net_buf_t *tx_queue_tail;
    u32 tx_queue_count;

    net_buf_t *accept_queue_head;
    net_buf_t *accept_queue_tail;
    u32 accept_queue_count;
    int backlog;
    
    /*
 * Callbacks
 */
    void (*connect_cb)(struct tcp_socket *sock, bool success);
    void (*recv_cb)(struct tcp_socket *sock, net_buf_t *buf);
    void (*close_cb)(struct tcp_socket *sock);
    
    /*
 * Private data
 */
    void *priv;
    
    list_head_t node;   /*
 * For a list of sockets
 */
    
    /*
 * Retransmission timer
 */
    u32 timer_expires;
    bool timer_active;
} tcp_socket_t;

/*
 * ============================================================================== Main functions ======================================================================================
 */

/*
 * TCP Initialization
 */
void tcp_init(void);

/*
 * Creating/Deleting a Socket
 */
tcp_socket_t *tcp_socket_create(void);
void tcp_socket_destroy(tcp_socket_t *sock);

/*
 * Bind and listen
 */
int tcp_bind(tcp_socket_t *sock, ipv4_addr_t addr, u16 port);
int tcp_listen(tcp_socket_t *sock, int backlog);

/*
 * Connection
 */
int tcp_connect(tcp_socket_t *sock, ipv4_addr_t addr, u16 port);

/*
 * Accepting connections
 */
tcp_socket_t *tcp_accept(tcp_socket_t *listen_sock);

/*
 * Sending and receiving data
 */
int tcp_send(tcp_socket_t *sock, const u8 *data, u32 len);
int tcp_recv(tcp_socket_t *sock, u8 *buf, u32 len);

/*
 * Closing
 */
int tcp_close(tcp_socket_t *sock);
int tcp_abort(tcp_socket_t *sock);

/*
 * Processing incoming TCP segments
 */
int tcp_input(net_buf_t *buf, ipv4_addr_t src, ipv4_addr_t dst,
              u16 src_port, u16 dst_port);

/*
 * Timer functions
 */
void tcp_timer_update(tcp_socket_t *sock, u32 timeout_ms);
void tcp_timer_handler(void);

/*
 * Pseudo-header for checksum
 */
typedef struct __attribute__((packed)) tcp_pseudo_header {
    u32 src_addr;
    u32 dst_addr;
    u8 zero;
    u8 protocol;
    u16 tcp_len;
} tcp_pseudo_header_t;

u16 tcp_checksum(tcp_header_t *tcp, u32 len, ipv4_addr_t src, ipv4_addr_t dst);

#endif /*
 * NET_TCP_H
 */
