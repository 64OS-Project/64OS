#include <net/ipv6.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <mm/heap.h>

/*
 * =============================================================================== Constant addresses ==================================================================================
 */

const ipv6_addr_t IPV6_ADDR_UNSPECIFIED = { .octets = {0} };
const ipv6_addr_t IPV6_ADDR_LOOPBACK = { .octets = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} };
const ipv6_addr_t IPV6_ADDR_ALL_NODES = { .octets = {0xff,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1} };
const ipv6_addr_t IPV6_ADDR_ALL_ROUTERS = { .octets = {0xff,2,0,0,0,0,0,0,0,0,0,0,0,0,0,2} };

static net_rx_handler_t ipv6_protocols[256];
static u32 ipv6_hop_limit = 64;

/*
 * ============================================================================== IPv6 addressing functions (full) ================================================================================
 */

static u8 hex_to_u8(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

ipv6_addr_t ipv6_pton(const char *str) {
    ipv6_addr_t addr = { .octets = {0} };
    u16 words[8] = {0};
    int word_idx = 0;
    int compress_idx = -1;
    int i = 0;
    
    if (!str) return addr;
    
    /*
 * Skip :: at the beginning
 */
    if (str[0] == ':' && str[1] == ':') {
        compress_idx = 0;
        i = 2;
    }
    
    while (*str && word_idx < 8) {
        if (*str == ':') {
            str++;
            if (*str == ':') {
                compress_idx = word_idx;
                str++;
                continue;
            }
            word_idx++;
            continue;
        }
        
        u16 val = 0;
        for (int j = 0; j < 4 && *str && *str != ':'; j++, str++) {
            val = (val << 4) | hex_to_u8(*str);
        }
        words[word_idx] = val;
        if (*str != ':') word_idx++;
    }
    
    /*
 * Expanding compression
 */
    if (compress_idx >= 0) {
        int num_words = word_idx;
        int num_missing = 8 - num_words;
        for (int j = 7; j >= compress_idx + num_missing; j--) {
            words[j] = words[j - num_missing];
        }
        for (int j = compress_idx; j < compress_idx + num_missing; j++) {
            words[j] = 0;
        }
    }
    
    /*
 * Convert words to bytes (big-endian)
 */
    for (int j = 0; j < 8; j++) {
        addr.octets[j*2] = (words[j] >> 8) & 0xFF;
        addr.octets[j*2+1] = words[j] & 0xFF;
    }
    
    return addr;
}

void ipv6_ntop(ipv6_addr_t addr, char *buf, u32 size) {
    u16 words[8];
    int compress_start = -1, compress_len = 0;
    int current_start = -1, current_len = 0;
    int i, j;
    
    /*
 * Converting bytes to words (big-endian)
 */
    for (i = 0; i < 8; i++) {
        words[i] = (addr.octets[i*2] << 8) | addr.octets[i*2+1];
    }
    
    /*
 * Finding the longest sequence of zeros to compress ::
 */
    for (i = 0; i < 8; i++) {
        if (words[i] == 0) {
            if (current_start == -1) {
                current_start = i;
                current_len = 1;
            } else {
                current_len++;
            }
        } else {
            if (current_len > compress_len) {
                compress_len = current_len;
                compress_start = current_start;
            }
            current_start = -1;
            current_len = 0;
        }
    }
    if (current_len > compress_len) {
        compress_len = current_len;
        compress_start = current_start;
    }
    
    char *ptr = buf;
    for (i = 0; i < 8; i++) {
        if (compress_start >= 0 && i == compress_start) {
            if (ptr != buf) *ptr++ = ':';
            *ptr++ = ':';
            i += compress_len - 1;
            continue;
        }
        
        if (i > 0) *ptr++ = ':';
        
        /*
 * Format the word in hex (without leading zeros)
 */
        if (words[i] >= 0x1000) {
            *ptr++ = "0123456789abcdef"[(words[i] >> 12) & 0xF];
            *ptr++ = "0123456789abcdef"[(words[i] >> 8) & 0xF];
            *ptr++ = "0123456789abcdef"[(words[i] >> 4) & 0xF];
            *ptr++ = "0123456789abcdef"[words[i] & 0xF];
        } else if (words[i] >= 0x100) {
            *ptr++ = "0123456789abcdef"[(words[i] >> 8) & 0xF];
            *ptr++ = "0123456789abcdef"[(words[i] >> 4) & 0xF];
            *ptr++ = "0123456789abcdef"[words[i] & 0xF];
        } else if (words[i] >= 0x10) {
            *ptr++ = "0123456789abcdef"[(words[i] >> 4) & 0xF];
            *ptr++ = "0123456789abcdef"[words[i] & 0xF];
        } else {
            *ptr++ = "0123456789abcdef"[words[i] & 0xF];
        }
    }
    *ptr = '\0';
}

bool ipv6_addr_is_multicast(ipv6_addr_t addr) {
    return addr.octets[0] == 0xFF;
}

bool ipv6_addr_is_link_local(ipv6_addr_t addr) {
    return (addr.octets[0] == 0xFE && (addr.octets[1] & 0xC0) == 0x80);
}

bool ipv6_addr_is_loopback(ipv6_addr_t addr) {
    return memcmp(&addr, &IPV6_ADDR_LOOPBACK, 16) == 0;
}

bool ipv6_addr_is_unspecified(ipv6_addr_t addr) {
    return memcmp(&addr, &IPV6_ADDR_UNSPECIFIED, 16) == 0;
}

/*
 * ============================================================================== IPv6 Checksum (pseudo-header for TCP/UDP/ICMPv6) ==================================================================================
 */

u16 ipv6_pseudo_checksum(ipv6_addr_t *src, ipv6_addr_t *dst, u8 next_header, u32 payload_len) {
    u32 sum = 0;
    u16 *ptr;
    int i;
    
    /*
 * Source address (16 bytes)
 */
    ptr = (u16*)src->octets;
    for (i = 0; i < 8; i++) {
        sum += ptr[i];
    }
    
    /*
 * Destination address (16 bytes)
 */
    ptr = (u16*)dst->octets;
    for (i = 0; i < 8; i++) {
        sum += ptr[i];
    }
    
    /*
 * Upper layer packet length
 */
    sum += (payload_len >> 16) + (payload_len & 0xFFFF);
    
    /*
 * Next header + zero
 */
    sum += next_header;
    
    /*
 * Fold
 */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/*
 * ============================================================================= Sending an IPv6 packet ================================================================================
 */

int ipv6_output(net_buf_t *buf, ipv6_addr_t *dest, ipv6_addr_t *src, u8 next_header) {
    if (!buf || !dest) return -1;
    
    net_reserve(buf, IPV6_HEADER_SIZE);
    
    ipv6_header_t *ip6h = (ipv6_header_t*)buf->data;
    memset(ip6h, 0, sizeof(ipv6_header_t));
    
    ip6h->version = htonl(0x60000000);
    ip6h->payload_len = htons(buf->len);
    ip6h->next_header = next_header;
    ip6h->hop_limit = ipv6_hop_limit;
    
    memcpy(ip6h->src_addr, src ? src->octets : IPV6_ADDR_UNSPECIFIED.octets, 16);
    memcpy(ip6h->dst_addr, dest->octets, 16);
    
    /*
 * TODO: interface selection and sending
 */
    /*
 * return net_tx(buf, dev);
 */
    
    return -1;  /*
 * No Ethernet driver yet
 */
}

/*
 * ============================================================================== Processing of incoming IPv6 packet ================================================================================
 */

void ipv6_input(net_buf_t *buf, net_device_t *dev) {
    ipv6_header_t *ip6h = (ipv6_header_t*)buf->data;
    
    /*
 * Version check
 */
    if (ntohl(ip6h->version) >> 28 != 6) {
        terminal_error_printf("[IPv6] Invalid version\n");
        return;
    }
    
    /*
 * Length check
 */
    if (buf->len < IPV6_HEADER_SIZE) {
        terminal_error_printf("[IPv6] Packet too short\n");
        return;
    }
    
    u16 payload_len = ntohs(ip6h->payload_len);
    if (buf->len < IPV6_HEADER_SIZE + payload_len) {
        terminal_error_printf("[IPv6] Truncated packet\n");
        return;
    }
    
    /*
 * Hop limit
 */
    if (ip6h->hop_limit <= 1) {
        /*
 * TODO: ICMPv6 Time Exceeded
 */
        return;
    }
    ip6h->hop_limit--;
    
    /*
 * Removing the title
 */
    net_pull(buf, IPV6_HEADER_SIZE);
    
    /*
 * We pass it to the upper level protocol
 */
    if (ip6h->next_header < 256 && ipv6_protocols[ip6h->next_header]) {
        ipv6_protocols[ip6h->next_header](buf, dev);
    } else {
        terminal_warn_printf("[IPv6] Unsupported next header %d\n", ip6h->next_header);
    }
    
    return;
}

/*
 * ============================================================================== Registering protocols ====================================================================================
 */

int ipv6_register_protocol(u8 next_header, net_rx_handler_t handler) {
    if (next_header >= 256) return -1;
    if (ipv6_protocols[next_header]) return -1;
    
    ipv6_protocols[next_header] = handler;
    terminal_printf("[IPv6] Registered protocol %d\n", next_header);
    return 0;
}

void ipv6_unregister_protocol(u8 next_header) {
    if (next_header < 256) {
        ipv6_protocols[next_header] = NULL;
    }
}

/*
 * =============================================================================== Initialization ======================================================================================
 */

void ipv6_init(void) {
    memset(ipv6_protocols, 0, sizeof(ipv6_protocols));
    terminal_printf("[IPv6] Initialized (full)\n");
}
