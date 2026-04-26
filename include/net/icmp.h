#ifndef NET_ICMP_H
#define NET_ICMP_H

#include <net/ipv4.h>

/*
 * =============================================================================== ICMP constants =================================================================================
 */

#define ICMP_TYPE_ECHO_REPLY        0
#define ICMP_TYPE_DEST_UNREACH      3
    #define ICMP_CODE_NET_UNREACH       0
    #define ICMP_CODE_HOST_UNREACH      1
    #define ICMP_CODE_PROT_UNREACH      2
    #define ICMP_CODE_PORT_UNREACH      3
    #define ICMP_CODE_FRAG_NEEDED       4
#define ICMP_TYPE_SOURCE_QUENCH     4
#define ICMP_TYPE_REDIRECT          5
#define ICMP_TYPE_ECHO_REQUEST      8
#define ICMP_TYPE_TIME_EXCEEDED     11
    #define ICMP_CODE_TTL_EXCEEDED      0
    #define ICMP_CODE_REASSEMBLY        1
#define ICMP_TYPE_PARAM_PROBLEM     12
#define ICMP_TYPE_TIMESTAMP         13
#define ICMP_TYPE_TIMESTAMP_REPLY   14

/*
 * =============================================================================== ICMP headers ====================================================================================
 */

typedef struct __attribute__((packed)) icmp_header {
    u8 type;
    u8 code;
    u16 checksum;
    u16 id;
    u16 sequence;
} icmp_header_t;

typedef struct __attribute__((packed)) icmp_unreach_header {
    u8 type;
    u8 code;
    u16 checksum;
    u16 unused;
    u16 mtu;
} icmp_unreach_header_t;

/*
 * ============================================================================================== Functions ====================================================================================
 */

/*
 * ICMP initialization
 */
void icmp_init(void);

/*
 * Sending an ICMP packet
 */
int icmp_output(net_buf_t *buf, ipv4_addr_t dest, ipv4_addr_t src, u8 type, u8 code);

/*
 * Incoming ICMP Processing
 */
int icmp_input(net_buf_t *buf, ipv4_addr_t src, ipv4_addr_t dst);

/*
 * Ping (echo request)
 */
int icmp_ping(ipv4_addr_t dest, u32 timeout_ms);

/*
 * ICMP checksum
 */
u16 icmp_checksum(u16 *data, u32 len);

#endif /*
 * NET_ICMP_H
 */
