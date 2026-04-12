#ifndef NET_DHCP_H
#define NET_DHCP_H

#include <net/udp.h>
#include <net/ipv4.h>

/*
 * =============================================================================== DHCP constants =================================================================================
 */

#define DHCP_SERVER_PORT        67
#define DHCP_CLIENT_PORT        68

#define DHCP_MAGIC_COOKIE       0x63825363

/*
 * DHCP Options
 */
#define DHCP_OPT_PAD            0
#define DHCP_OPT_SUBNET_MASK    1
#define DHCP_OPT_ROUTER         3
#define DHCP_OPT_DNS_SERVER     6
#define DHCP_OPT_HOSTNAME       12
#define DHCP_OPT_DOMAIN_NAME    15
#define DHCP_OPT_REQUESTED_IP   50
#define DHCP_OPT_LEASE_TIME     51
#define DHCP_OPT_MSG_TYPE       53
#define DHCP_OPT_SERVER_ID      54
#define DHCP_OPT_PARAM_LIST     55
#define DHCP_OPT_MAX_MSG_SIZE   57
#define DHCP_OPT_VENDOR_ID      60
#define DHCP_OPT_CLIENT_ID      61
#define DHCP_OPT_END            255

/*
 * DHCP messages
 */
#define DHCP_DISCOVER           1
#define DHCP_OFFER              2
#define DHCP_REQUEST            3
#define DHCP_DECLINE            4
#define DHCP_ACK                5
#define DHCP_NAK                6
#define DHCP_RELEASE            7
#define DHCP_INFORM             8

/*
 * =============================================================================== DHCP header ================================================================================
 */

typedef struct __attribute__((packed)) dhcp_header {
    u8 op;              /*
 * 1 = request, 2 = reply
 */
    u8 htype;           /*
 * Hardware type (1 = Ethernet)
 */
    u8 hlen;            /*
 * Hardware address length (6)
 */
    u8 hops;            /*
 * Hops
 */
    u32 xid;            /*
 * Transaction ID
 */
    u16 secs;           /*
 * Seconds since boot
 */
    u16 flags;          /*
 * Flags
 */
    u32 ciaddr;         /*
 * Client IP address
 */
    u32 yiaddr;         /*
 * Your IP address
 */
    u32 siaddr;         /*
 * Server IP address
 */
    u32 giaddr;         /*
 * Gateway IP address
 */
    u8 chaddr[16];      /*
 * Client hardware address
 */
    u8 sname[64];       /*
 * Server name
 */
    u8 file[128];       /*
 * Boot file name
 */
    u32 magic;          /*
 * Magic cookie
 */
    u8 options[];       /*
 * Options
 */
} dhcp_header_t;

/*
 * ============================================================================== Preliminary declaration of the dhcp_client structure =================================================================================
 */

struct dhcp_client;

/*
 * Type of callback function (after preliminary declaration)
 */
typedef void (*dhcp_callback_t)(struct dhcp_client *client, bool success);

/*
 * ============================================================================== DHCP client (full definition) ===============================================================================
 */

typedef struct dhcp_client {
    net_device_t *dev;
    bool running;
    bool configured;
    
    u32 xid;                    /*
 * Transaction ID
 */
    u32 yiaddr;                 /*
 * Our IP address
 */
    u32 server_ip;              /*
 * DHCP server IP
 */
    u32 subnet_mask;            /*
 * Subnet mask
 */
    u32 gateway;                /*
 * Default gateway
 */
    u32 dns_server;             /*
 * DNS server
 */
    u32 lease_time;             /*
 * Lease time in seconds
 */
    
    u64 lease_expires;          /*
 * Timestamp when lease expires
 */
    u64 renew_time;             /*
 * Renewal time (T1)
 */
    u64 rebind_time;            /*
 * Rebind time (T2)
 */
    
    dhcp_callback_t callback;   /*
 * Callback function
 */
} dhcp_client_t;

/*
 * ============================================================================================== Functions ====================================================================================
 */

/*
 * Initializing the DHCP client
 */
int dhcp_init(dhcp_client_t *client, net_device_t *dev);

/*
 * Starting DHCP (Discover -> Request -> Ack)
 */
int dhcp_start(dhcp_client_t *client);

/*
 * Requesting a lease (renew)
 */
int dhcp_renew(dhcp_client_t *client);

/*
 * IP release
 */
int dhcp_release(dhcp_client_t *client);

/*
 * Processing an incoming DHCP packet
 */
void dhcp_input(net_buf_t *buf, ipv4_addr_t src, ipv4_addr_t dst,
                u16 src_port, u16 dst_port);

/*
 * Initializing the DHCP subsystem
 */
void dhcp_subsystem_init(void);

/*
 * Run DHCP on all interfaces
 */
void dhcp_start_on_all_interfaces(dhcp_callback_t callback);

#endif /*
 * NET_DHCP_H
 */
