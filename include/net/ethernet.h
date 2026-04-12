#ifndef NET_ETHERNET_H
#define NET_ETHERNET_H

#include <net/net.h>

/*
 * ============================================================================== Ethernet constants ======================================================================================
 */

#define ETH_ALEN            6
#define ETH_HLEN            14
#define ETH_FRAME_LEN       1514
#define ETH_MIN_FRAME       64

/*
 * EtherType
 */
#define ETH_P_IP            0x0800
#define ETH_P_ARP           0x0806
#define ETH_P_IPV6          0x86DD

/*
 * ============================================================================== Ethernet header =======================================================================================
 */

typedef struct __attribute__((packed)) eth_header {
    u8 dst_mac[ETH_ALEN];
    u8 src_mac[ETH_ALEN];
    u16 type;
} eth_header_t;

/*
 * ============================================================================================== Functions ====================================================================================
 */

/*
 * Ethernet initialization
 */
void ethernet_init(void);

/*
 * Sending an Ethernet frame
 */
int ethernet_output(net_buf_t *buf, net_device_t *dev, u8 *dst_mac, u16 type);

/*
 * Processing of incoming Ethernet frame
 */
void ethernet_input(net_buf_t *buf, net_device_t *dev);

/*
 * MAC address to string
 */
void ethernet_mac_ntop(u8 *mac, char *buf, u32 size);
int ethernet_mac_pton(const char *str, u8 *mac);

/*
 * Comparison of MAC addresses
 */
bool ethernet_mac_equal(u8 *mac1, u8 *mac2);

/*
 * Broadcast MAC
 */
extern const u8 ETH_MAC_BROADCAST[ETH_ALEN];

#endif /*
 * NET_ETHERNET_H
 */
