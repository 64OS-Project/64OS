#ifndef NET_NET_H
#define NET_NET_H

#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/timer.h>

/*
 * =============================================================================== Constants =====================================================================================
 */

#define NET_MTU             1500
#define NET_HEADROOM        64
#define NET_BUFFER_SIZE     (NET_MTU + NET_HEADROOM)

#define ETH_ALEN            6
#define IP_ALEN             4
#define IP6_ALEN            16

/*
 * Protocols
 */
#define ETH_P_IP            0x0800
#define ETH_P_ARP           0x0806
#define ETH_P_IPV6          0x86DD

/*
 * IP protocols
 */
#define IP_PROTO_ICMP       1
#define IP_PROTO_TCP        6
#define IP_PROTO_UDP        17

static inline u16 ntohs(u16 n) {
    return (n >> 8) | (n << 8);
}

static inline u16 htons(u16 n) {
    return (n >> 8) | (n << 8);
}

static inline u32 ntohl(u32 n) {
    return ((n & 0xFF) << 24) |
           ((n & 0xFF00) << 8) |
           ((n & 0xFF0000) >> 8) |
           ((n & 0xFF000000) >> 24);
}

static inline u32 htonl(u32 n) {
    return ((n & 0xFF) << 24) |
           ((n & 0xFF00) << 8) |
           ((n & 0xFF0000) >> 8) |
           ((n & 0xFF000000) >> 24);
}

/*
 * ============================================================================== Structures ========================================================================================
 */

typedef enum {
    NET_TYPE_NONE = 0,
    NET_TYPE_ETHERNET = 1,
    NET_TYPE_WIFI = 2,
    NET_TYPE_LOOPBACK = 3,
    NET_TYPE_PPP = 4,
    NET_TYPE_BRIDGE = 5,
} net_device_type_t;

/*
 * Network buffer (sk_buff-like)
 */
typedef struct net_buf {
    u8 *data;                   /*
 * Pointer to data
 */
    u8 *head;                   /*
 * Start of buffer
 */
    u8 *tail;                   /*
 * End of data
 */
    u8 *end;                    /*
 * End of buffer
 */
    
    u32 len;                    /*
 * Data length
 */
    u32 headroom;               /*
 * Free before data
 */
    u32 truesize;               /*
 * Real Buffer Size
 */
    
    u16 protocol;               /*
 * Protocol (EtherType)
 */
    u32 ifindex;                /*
 * Interface
 */
    
    struct net_device *dev;     /*
 * Device
 */
    struct net_buf *next;       /*
 * For queues
 */
} net_buf_t;

/*
 * Network device
 */
typedef struct net_device {
    char name[16];              /*
 * eth0, eth1, etc
 */
    u32 index;                  /*
 * Interface index
 */
    net_device_type_t type;
    
    /*
 * Addresses
 */
    u8 mac_addr[ETH_ALEN];
    u8 ip_addr[IP_ALEN];
    u8 netmask[IP_ALEN];
    u8 gateway[IP_ALEN];
    
    /*
 * State
 */
    bool up;                    /*
 * The interface is up
 */
    bool running;
    
    u32 mtu;                    /*
 * Maximum Transmission Unit
 */
    
    /*
 * Driver functions
 */
    int (*open)(struct net_device *dev);
    int (*stop)(struct net_device *dev);
    int (*xmit)(struct net_buf *buf, struct net_device *dev);
    
    /*
 * Driver private data
 */
    void *priv;
    
    list_head_t node;           /*
 * For device list
 */
} net_device_t;

/*
 * Packet queue for RX/TX
 */
typedef struct net_queue {
    net_buf_t *head;
    net_buf_t *tail;
    u32 count;
    u32 max_len;
} net_queue_t;

/*
 * Statistics
 */
typedef struct net_stats {
    u64 rx_packets;
    u64 rx_bytes;
    u64 rx_errors;
    u64 rx_dropped;
    
    u64 tx_packets;
    u64 tx_bytes;
    u64 tx_errors;
    u64 tx_dropped;
} net_stats_t;

/*
 * ============================================================================== Global functions ======================================================================================= Global functions
 */

/*
 * Initializing the network stack
 */
void net_init(void);

/*
 * Registering a Network Device
 */
int net_register_device(net_device_t *dev);
int net_unregister_device(net_device_t *dev);

/*
 * Sending/receiving packages
 */
int net_rx(net_buf_t *buf, net_device_t *dev);
int net_tx(net_buf_t *buf, net_device_t *dev);

/*
 * Buffer management
 */
net_buf_t *net_alloc_buf(u32 size);
void net_free_buf(net_buf_t *buf);
void net_reserve(net_buf_t *buf, u32 headroom);
void net_put(net_buf_t *buf, u32 len);
void net_pull(net_buf_t *buf, u32 len);

/*
 * Protocol registrations
 */
typedef void (*net_rx_handler_t)(net_buf_t *buf, net_device_t *dev);

int net_register_protocol(u16 eth_type, net_rx_handler_t handler);
void net_unregister_protocol(u16 eth_type);

/*
 * Getting a device by index/name
 */
net_device_t *net_get_device(u32 index);
net_device_t *net_get_device_by_name(const char *name);

/*
 * Statistics
 */
void net_print_stats(void);

/*
 * Get the first active network interface
 */
net_device_t *net_get_first_active(void);

/*
 * Get any network interface (first in the list)
 */
net_device_t *net_get_first_device(void);

int net_get_devices_by_type(net_device_type_t type, net_device_t **devices, int max);

#endif /*
 * NET_NET_H
 */
