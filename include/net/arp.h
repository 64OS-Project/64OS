#ifndef NET_ARP_H
#define NET_ARP_H

#include <net/ethernet.h>
#include <net/ipv4.h>

/*
 * ============================================================================== ARP constants =================================================================================
 */

#define ARP_HRD_ETHERNET    0x0001
#define ARP_PRO_IP          0x0800
#define ARP_HLEN_ETHERNET   6
#define ARP_PLEN_IP         4

#define ARP_OP_REQUEST      0x0001
#define ARP_OP_REPLY        0x0002

/*
 * ============================================================================== ARP header =================================================================================
 */

typedef struct __attribute__((packed)) arp_header {
    u16 hrd;            /*
 * Hardware type (Ethernet = 1)
 */
    u16 pro;            /*
 * Protocol type (IPv4 = 0x0800)
 */
    u8  hln;            /*
 * Hardware address length (6)
 */
    u8  pln;            /*
 * Protocol address length (4)
 */
    u16 op;             /*
 * Operation (1=request, 2=reply)
 */
    u8  sha[ETH_ALEN];  /*
 * Sender hardware address
 */
    u32 spa;            /*
 * Sender protocol address
 */
    u8  tha[ETH_ALEN];  /*
 * Target hardware address
 */
    u32 tpa;            /*
 * Target protocol address
 */
} arp_header_t;

/*
 * ============================================================================== ARP cache ====================================================================================
 */

#define ARP_CACHE_SIZE      32
#define ARP_CACHE_TIMEOUT   300000   /*
 * 5 minutes in milliseconds
 */

typedef struct arp_cache_entry {
    ipv4_addr_t ip;
    u8 mac[ETH_ALEN];
    u64 timestamp;
    bool valid;
} arp_cache_entry_t;

/*
 * ============================================================================================== Functions ====================================================================================
 */

/*
 * ARP initialization
 */
void arp_init(void);

/*
 * Sending an ARP request
 */
int arp_request(ipv4_addr_t ip, net_device_t *dev);

/*
 * Processing an incoming ARP packet
 */
void arp_input(net_buf_t *buf, net_device_t *dev);

/*
 * IP → MAC resolution (blocking with timeout)
 */
int arp_resolve(net_device_t *dev, ipv4_addr_t ip, u8 *mac, u32 timeout_ms);

/*
 * Adding/removing a cache entry
 */
void arp_cache_add(ipv4_addr_t ip, u8 *mac);
void arp_cache_del(ipv4_addr_t ip);
void arp_cache_clear(void);
void arp_cache_purge_expired(void);

/*
 * Cache search
 */
bool arp_cache_lookup(ipv4_addr_t ip, u8 *mac);

/*
 * Cache output
 */
void arp_cache_print(void);

#endif /*
 * NET_ARP_H
 */
