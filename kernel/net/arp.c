#include <net/arp.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <kernel/timer.h>

static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static bool arp_pending_requests[ARP_CACHE_SIZE];

/*
 * ============================================================================== ARP cache functions =================================================================================
 */

void arp_cache_add(ipv4_addr_t ip, u8 *mac) {
    int oldest = -1;
    u64 oldest_time = ~0ULL;
    
    /*
 * Looking for an existing entry or free space
 */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip.addr == ip.addr) {
            /*
 * Updating an existing one
 */
            memcpy(arp_cache[i].mac, mac, ETH_ALEN);
            arp_cache[i].timestamp = timer_apic_ticks();
            return;
        }
        if (!arp_cache[i].valid) {
            oldest = i;
            break;
        }
        if (arp_cache[i].timestamp < oldest_time) {
            oldest_time = arp_cache[i].timestamp;
            oldest = i;
        }
    }
    
    /*
 * Add a new entry (overwrite the oldest one if necessary)
 */
    if (oldest >= 0) {
        arp_cache[oldest].ip = ip;
        memcpy(arp_cache[oldest].mac, mac, ETH_ALEN);
        arp_cache[oldest].timestamp = timer_apic_ticks();
        arp_cache[oldest].valid = true;
        
        char ip_str[16];
        char mac_str[18];
        ipv4_ntop(ip, ip_str, 16);
        ethernet_mac_ntop(mac, mac_str, 18);
        terminal_printf("[ARP] Added: %s -> %s\n", ip_str, mac_str);
    }
}

void arp_cache_del(ipv4_addr_t ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip.addr == ip.addr) {
            arp_cache[i].valid = false;
            return;
        }
    }
}

void arp_cache_clear(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
}

void arp_cache_purge_expired(void) {
    u64 now = timer_apic_ticks();
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid) {
            if (now - arp_cache[i].timestamp > ARP_CACHE_TIMEOUT) {
                arp_cache[i].valid = false;
            }
        }
    }
}

bool arp_cache_lookup(ipv4_addr_t ip, u8 *mac) {
    arp_cache_purge_expired();
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip.addr == ip.addr) {
            if (mac) memcpy(mac, arp_cache[i].mac, ETH_ALEN);
            return true;
        }
    }
    return false;
}

void arp_cache_print(void) {
    terminal_printf("\n=== ARP Cache ===\n");
    terminal_printf("%-16s %-18s %s\n", "IP Address", "MAC Address", "Age (ms)");
    terminal_printf("---------------- ----------------- ----------\n");
    
    u64 now = timer_apic_ticks();
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid) {
            char ip_str[16];
            char mac_str[18];
            ipv4_ntop(arp_cache[i].ip, ip_str, 16);
            ethernet_mac_ntop(arp_cache[i].mac, mac_str, 18);
            
            u64 age = now - arp_cache[i].timestamp;
            terminal_printf("%-16s %-18s %llu\n", ip_str, mac_str, age);
        }
    }
    terminal_printf("================\n");
}

/*
 * ============================================================================== ARP packets ===================================================================================
 */

int arp_request(ipv4_addr_t ip, net_device_t *dev) {
    net_buf_t *buf = net_alloc_buf(sizeof(arp_header_t));
    if (!buf) return -1;
    
    arp_header_t *arp = (arp_header_t*)buf->data;
    memset(arp, 0, sizeof(arp_header_t));
    
    arp->hrd = htons(ARP_HRD_ETHERNET);
    arp->pro = htons(ARP_PRO_IP);
    arp->hln = ARP_HLEN_ETHERNET;
    arp->pln = ARP_PLEN_IP;
    arp->op = htons(ARP_OP_REQUEST);
    
    /*
 * Sender
 */
    memcpy(arp->sha, dev->mac_addr, ETH_ALEN);
    arp->spa = ((ipv4_addr_t*)dev->ip_addr)->addr;
    
    /*
 * Target (unknown)
 */
    memset(arp->tha, 0, ETH_ALEN);
    arp->tpa = ip.addr;
    
    buf->len = sizeof(arp_header_t);
    
    char ip_str[16];
    ipv4_ntop(ip, ip_str, 16);
    terminal_printf("[ARP] Request for %s on %s\n", ip_str, dev->name);
    
    return ethernet_output(buf, dev, (u8*)ETH_MAC_BROADCAST, ETH_P_ARP);
}

void arp_input(net_buf_t *buf, net_device_t *dev) {
    if (buf->len < sizeof(arp_header_t)) return;
    
    arp_header_t *arp = (arp_header_t*)buf->data;
    
    /*
 * Checking the type
 */
    if (ntohs(arp->hrd) != ARP_HRD_ETHERNET) return;
    if (ntohs(arp->pro) != ARP_PRO_IP) return;
    if (arp->hln != ARP_HLEN_ETHERNET || arp->pln != ARP_PLEN_IP) return;
    
    ipv4_addr_t sender_ip = { .addr = arp->spa };
    ipv4_addr_t target_ip = { .addr = arp->tpa };
    ipv4_addr_t my_ip = { .addr = ((ipv4_addr_t*)dev->ip_addr)->addr };
    
    /*
 * Add the sender to the cache
 */
    if (sender_ip.addr != 0) {
        arp_cache_add(sender_ip, arp->sha);
    }
    
    u16 op = ntohs(arp->op);
    
    switch (op) {
        case ARP_OP_REQUEST:
            /*
 * Is this for us?
 */
            if (target_ip.addr == my_ip.addr) {
                /*
 * Sending an ARP reply
 */
                net_buf_t *reply = net_alloc_buf(sizeof(arp_header_t));
                if (!reply) break;
                
                arp_header_t *reply_arp = (arp_header_t*)reply->data;
                memcpy(reply_arp, arp, sizeof(arp_header_t));
                
                reply_arp->op = htons(ARP_OP_REPLY);
                memcpy(reply_arp->tha, arp->sha, ETH_ALEN);
                reply_arp->tpa = arp->spa;
                memcpy(reply_arp->sha, dev->mac_addr, ETH_ALEN);
                reply_arp->spa = my_ip.addr;
                
                reply->len = sizeof(arp_header_t);
                
                char ip_str[16];
                ipv4_ntop(sender_ip, ip_str, 16);
                terminal_printf("[ARP] Reply to %s\n", ip_str);
                
                ethernet_output(reply, dev, arp->sha, ETH_P_ARP);
            }
            break;
            
        case ARP_OP_REPLY:
            /*
 * The answer has already been processed (added to the cache above)
 */
            break;
    }
    
    return;
}

/*
 * =============================================================================== Resolution with timeout ================================================================================
 */

static struct {
    ipv4_addr_t ip;
    u8 mac[ETH_ALEN];
    bool completed;
    bool waiting;
} arp_pending;

static void arp_resolve_timeout(u64 *data) {
    (void)data;
    if (!arp_pending.completed && arp_pending.waiting) {
        arp_pending.completed = true;
        arp_pending.waiting = false;
        terminal_warn_printf("[ARP] Resolve timeout\n");
    }
}

int arp_resolve(net_device_t *dev, ipv4_addr_t ip, u8 *mac, u32 timeout_ms) {
    /*
 * First we check the cache
 */
    if (arp_cache_lookup(ip, mac)) {
        return 0;
    }
    
    /*
 * Sending an ARP request
 */
    arp_request(ip, dev);
    
    /*
 * Waiting for a response with a timeout
 */
    arp_pending.ip = ip;
    arp_pending.completed = false;
    arp_pending.waiting = true;
    
    /*
 * TODO: set timer
 */
    /*
 * timer_add(timeout_ms, arp_resolve_timeout, NULL);
 */
    
    /*
 * Simple wait loop (real code needs a scheduler)
 */
    u64 start = timer_apic_ticks();
    while (!arp_pending.completed && (timer_apic_ticks() - start) < timeout_ms) {
        asm volatile("pause");
    }
    
    arp_pending.waiting = false;
    
    if (arp_cache_lookup(ip, mac)) {
        return 0;
    }
    
    return -1;
}

/*
 * =============================================================================== Initialization ======================================================================================
 */

void arp_init(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(&arp_pending, 0, sizeof(arp_pending));
    terminal_printf("[ARP] Initialized\n");
}
