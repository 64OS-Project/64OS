#include <net/icmp.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <kernel/timer.h>

static struct {
    bool waiting;
    u16 id;
    u16 sequence;
    u64 timestamp;
    bool completed;
} icmp_ping_state;

/*
 * ============================================================================= ICMP Checksum ============================================================================
 */

u16 icmp_checksum(u16 *data, u32 len) {
    u32 sum = 0;
    
    while (len > 1) {
        sum += *data++;
        len -= 2;
    }
    
    if (len) {
        sum += *(u8*)data;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/*
 * ============================================================================== Sending ICMP =================================================================================
 */

int icmp_output(net_buf_t *buf, ipv4_addr_t dest, ipv4_addr_t src, u8 type, u8 code) {
    icmp_header_t *icmp = (icmp_header_t*)buf->data;
    
    icmp->type = type;
    icmp->code = code;
    icmp->checksum = 0;
    icmp->checksum = icmp_checksum((u16*)icmp, buf->len);
    
    return ipv4_output(buf, dest, src, IP_PROTO_ICMP);
}

/*
 * =============================================================================== Processing incoming ICMP =================================================================================
 */

int icmp_input(net_buf_t *buf, ipv4_addr_t src, ipv4_addr_t dst) {
    if (buf->len < sizeof(icmp_header_t)) return -1;
    
    icmp_header_t *icmp = (icmp_header_t*)buf->data;
    
    /*
 * Checking the checksum
 */
    u16 checksum = icmp->checksum;
    icmp->checksum = 0;
    if (icmp_checksum((u16*)icmp, buf->len) != checksum) {
        terminal_error_printf("[ICMP] Bad checksum\n");
        return -1;
    }
    icmp->checksum = checksum;
    
    char src_str[16], dst_str[16];
    ipv4_ntop(src, src_str, 16);
    ipv4_ntop(dst, dst_str, 16);
    
    switch (icmp->type) {
        case ICMP_TYPE_ECHO_REQUEST:
            /*
 * We answer ping
 */
            icmp->type = ICMP_TYPE_ECHO_REPLY;
            icmp->checksum = 0;
            icmp->checksum = icmp_checksum((u16*)icmp, buf->len);
            
            terminal_printf("[ICMP] Echo reply to %s\n", src_str);
            
            ipv4_output(buf, src, dst, IP_PROTO_ICMP);
            break;
            
        case ICMP_TYPE_ECHO_REPLY:
            /*
 * Received a response to ping
 */
            if (icmp_ping_state.waiting &&
                icmp_ping_state.id == ntohs(icmp->id) &&
                icmp_ping_state.sequence == ntohs(icmp->sequence)) {
                
                u64 rtt = timer_apic_ticks() - icmp_ping_state.timestamp;
                terminal_printf("[ICMP] Echo reply from %s: id=%d, seq=%d, rtt=%llu ms\n",
                               src_str, ntohs(icmp->id), ntohs(icmp->sequence), rtt);
                icmp_ping_state.completed = true;
            }
            break;
            
        case ICMP_TYPE_TIME_EXCEEDED:
            terminal_printf("[ICMP] Time exceeded from %s\n", src_str);
            break;
            
        case ICMP_TYPE_DEST_UNREACH:
            terminal_printf("[ICMP] Destination unreachable from %s (code=%d)\n",
                           src_str, icmp->code);
            break;
            
        default:
            terminal_warn_printf("[ICMP] Unknown type %d from %s\n", icmp->type, src_str);
            break;
    }
    
    return 0;
}

/*
 * ============================================================================= Ping ============================================================================
 */

int icmp_ping(ipv4_addr_t dest, u32 timeout_ms) {
    net_buf_t *buf = net_alloc_buf(64);  /*
 * 64 bytes of data
 */
    if (!buf) return -1;
    
    icmp_header_t *icmp = (icmp_header_t*)buf->data;
    memset(icmp, 0, sizeof(icmp_header_t));
    
    static u16 ping_id = 0x1234;
    static u16 ping_seq = 0;
    
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = htons(ping_id++);
    icmp->sequence = htons(ping_seq++);
    
    /*
 * Fill in the data (timestamp is possible)
 */
    u8 *data = (u8*)icmp + sizeof(icmp_header_t);
    for (int i = 0; i < 56; i++) {
        data[i] = i & 0xFF;
    }
    buf->len = sizeof(icmp_header_t) + 56;
    
    /*
 * Saving state for response
 */
    icmp_ping_state.waiting = true;
    icmp_ping_state.id = ntohs(icmp->id);
    icmp_ping_state.sequence = ntohs(icmp->sequence);
    icmp_ping_state.timestamp = timer_apic_ticks();
    icmp_ping_state.completed = false;
    
    char dest_str[16];
    ipv4_ntop(dest, dest_str, 16);
    terminal_printf("[ICMP] Ping to %s (id=%d, seq=%d)\n",
                   dest_str, icmp_ping_state.id, icmp_ping_state.sequence);
    
    /*
 * We send
 */
    int ret = icmp_output(buf, dest, (ipv4_addr_t){0}, ICMP_TYPE_ECHO_REQUEST, 0);
    
    /*
 * Waiting for a response with a timeout
 */
    u64 start = timer_apic_ticks();
    while (!icmp_ping_state.completed && (timer_apic_ticks() - start) < timeout_ms) {
        asm volatile("pause");
    }
    
    icmp_ping_state.waiting = false;
    
    if (!icmp_ping_state.completed) {
        terminal_error_printf("[ICMP] Ping timeout\n");
        return -1;
    }
    
    return 0;
}

/*
 * =============================================================================== Initialization ======================================================================================
 */

void icmp_init(void) {
    memset(&icmp_ping_state, 0, sizeof(icmp_ping_state));
    
    /*
 * Registering ICMP in IPv4
 */
    ipv4_register_protocol(IP_PROTO_ICMP, (net_rx_handler_t)icmp_input);
    
    terminal_printf("[ICMP] Initialized\n");
}
