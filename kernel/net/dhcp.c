#include <net/dhcp.h>
#include <net/udp.h>
#include <net/ipv4.h>
#include <libk/string.h>
#include <kernel/terminal.h>
#include <kernel/timer.h>
#include <mm/heap.h>
#include <crypto/api.h>

/*
 * List of active DHCP clients
 */
typedef struct dhcp_client_entry {
    dhcp_client_t client;
    struct dhcp_client_entry *next;
} dhcp_client_entry_t;

static dhcp_client_entry_t *g_dhcp_clients = NULL;
static u32 g_dhcp_clients_count = 0;

extern list_head_t net_devices;

/*
 * =============================================================================== Auxiliary functions ================================================================================
 */

static u32 dhcp_generate_xid(void) {
    u32 xid;
    crypto_get_random_bytes((u8*)&xid, sizeof(xid));
    return xid;
}

static void dhcp_add_option(u8 *options, int *offset, u8 type, u8 len, void *data) {
    options[(*offset)++] = type;
    options[(*offset)++] = len;
    memcpy(options + *offset, data, len);
    *offset += len;
}

static void dhcp_add_option_byte(u8 *options, int *offset, u8 type, u8 value) {
    options[(*offset)++] = type;
    options[(*offset)++] = 1;
    options[(*offset)++] = value;
}

static void dhcp_add_option_dword(u8 *options, int *offset, u8 type, u32 value) {
    options[(*offset)++] = type;
    options[(*offset)++] = 4;
    *(u32*)(options + *offset) = htonl(value);
    *offset += 4;
}

static void dhcp_add_option_end(u8 *options, int *offset) {
    options[(*offset)++] = DHCP_OPT_END;
}

static u8* dhcp_find_option(dhcp_header_t *dhcp, u8 opt_type, int *len) {
    u8 *opt = dhcp->options;
    int offset = 0;
    
    while (offset < 300) {
        u8 type = opt[offset++];
        if (type == DHCP_OPT_END) break;
        if (type == DHCP_OPT_PAD) continue;
        
        u8 opt_len = opt[offset++];
        
        if (type == opt_type) {
            if (len) *len = opt_len;
            return opt + offset;
        }
        
        offset += opt_len;
    }
    
    return NULL;
}

/*
 * ============================================================================= Sending a DHCP message ================================================================================ Sending a DHCP message
 */

static int dhcp_send_message(dhcp_client_t *client, u8 msg_type, u32 requested_ip) {
    net_buf_t *buf = net_alloc_buf(sizeof(dhcp_header_t) + 312);
    if (!buf) return -1;
    
    dhcp_header_t *dhcp = (dhcp_header_t*)buf->data;
    memset(dhcp, 0, sizeof(dhcp_header_t));
    
    dhcp->op = 1;
    dhcp->htype = 1;
    dhcp->hlen = 6;
    dhcp->xid = htonl(client->xid);
    dhcp->secs = htons(0);
    dhcp->flags = htons(0x8000);
    
    memcpy(dhcp->chaddr, client->dev->mac_addr, 6);
    dhcp->magic = htonl(DHCP_MAGIC_COOKIE);
    
    int opt_offset = 0;
    u8 *options = dhcp->options;
    
    dhcp_add_option_byte(options, &opt_offset, DHCP_OPT_MSG_TYPE, msg_type);
    
    u8 client_id[7] = {1};
    memcpy(client_id + 1, client->dev->mac_addr, 6);
    dhcp_add_option(options, &opt_offset, DHCP_OPT_CLIENT_ID, 7, client_id);
    
    if (requested_ip != 0) {
        dhcp_add_option_dword(options, &opt_offset, DHCP_OPT_REQUESTED_IP, requested_ip);
    }
    
    u8 param_list[] = {1, 3, 6, 51};
    dhcp_add_option(options, &opt_offset, DHCP_OPT_PARAM_LIST, sizeof(param_list), param_list);
    
    char hostname[] = "64OS";
    dhcp_add_option(options, &opt_offset, DHCP_OPT_HOSTNAME, sizeof(hostname) - 1, hostname);
    
    dhcp_add_option_end(options, &opt_offset);
    
    buf->len = sizeof(dhcp_header_t) + opt_offset;
    
    ipv4_addr_t dest = { .addr = 0xFFFFFFFF };
    ipv4_addr_t src = { .addr = 0 };
    
    const char *msg_names[] = {"", "DISCOVER", "OFFER", "REQUEST", "DECLINE", "ACK", "NAK", "RELEASE", "INFORM"};
    const char *msg_name = (msg_type < 9) ? msg_names[msg_type] : "UNKNOWN";
    
    terminal_printf("[DHCP] %s: %s (xid=0x%x)\n", client->dev->name, msg_name, client->xid);
    
    return udp_output(buf, dest, src, DHCP_SERVER_PORT, DHCP_CLIENT_PORT);
}

/*
 * ============================================================================== Processing the DHCP response =================================================================================
 */

static void dhcp_process_response(dhcp_client_t *client, dhcp_header_t *dhcp) {
    u32 xid = ntohl(dhcp->xid);
    if (xid != client->xid) return;
    
    int opt_len;
    u8 *opt = dhcp_find_option(dhcp, DHCP_OPT_MSG_TYPE, &opt_len);
    if (!opt || opt_len < 1) return;
    
    u8 msg_type = opt[0];
    
    switch (msg_type) {
        case DHCP_OFFER: {
            char ip_str[16];

            client->yiaddr = ntohl(dhcp->yiaddr);
            
            opt = dhcp_find_option(dhcp, DHCP_OPT_SERVER_ID, &opt_len);
            if (opt && opt_len >= 4) {
                client->server_ip = ntohl(*(u32*)opt);
            }
            
            opt = dhcp_find_option(dhcp, DHCP_OPT_SUBNET_MASK, &opt_len);
            if (opt && opt_len >= 4) {
                client->subnet_mask = ntohl(*(u32*)opt);
            }
            
            opt = dhcp_find_option(dhcp, DHCP_OPT_ROUTER, &opt_len);
            if (opt && opt_len >= 4) {
                client->gateway = ntohl(*(u32*)opt);
            }
            
            opt = dhcp_find_option(dhcp, DHCP_OPT_DNS_SERVER, &opt_len);
            if (opt && opt_len >= 4) {
                client->dns_server = ntohl(*(u32*)opt);
            }
            
            opt = dhcp_find_option(dhcp, DHCP_OPT_LEASE_TIME, &opt_len);
            if (opt && opt_len >= 4) {
                client->lease_time = ntohl(*(u32*)opt);
            }

            ipv4_ntop((ipv4_addr_t){ .addr = client->yiaddr }, ip_str, 16);
            terminal_printf("[DHCP] %s: OFFER %s\n", client->dev->name, ip_str);
            
            dhcp_send_message(client, DHCP_REQUEST, client->yiaddr);
            break;
        }
            
        case DHCP_ACK: {
            client->configured = true;
            
            memcpy(client->dev->ip_addr, &client->yiaddr, 4);
            memcpy(client->dev->netmask, &client->subnet_mask, 4);
            memcpy(client->dev->gateway, &client->gateway, 4);
            
            ipv4_route_add((ipv4_addr_t){ .addr = client->yiaddr & client->subnet_mask },
                          (ipv4_addr_t){ .addr = client->subnet_mask },
                          (ipv4_addr_t){0},
                          (ipv4_addr_t){ .addr = client->yiaddr },
                          client->dev, 0);
            
            if (client->gateway != 0) {
                ipv4_route_add((ipv4_addr_t){0}, (ipv4_addr_t){0},
                              (ipv4_addr_t){ .addr = client->gateway },
                              (ipv4_addr_t){ .addr = client->yiaddr },
                              client->dev, 0);
            }
            
            u64 now = timer_apic_ticks() / 1000;
            client->lease_expires = now + client->lease_time;
            client->renew_time = now + client->lease_time / 2;
            client->rebind_time = now + client->lease_time * 7 / 8;
            
            char ip_str[16], mask_str[16], gw_str[16];
            ipv4_ntop((ipv4_addr_t){ .addr = client->yiaddr }, ip_str, 16);
            ipv4_ntop((ipv4_addr_t){ .addr = client->subnet_mask }, mask_str, 16);
            ipv4_ntop((ipv4_addr_t){ .addr = client->gateway }, gw_str, 16);
            
            terminal_success_printf("[DHCP] %s: IP=%s MASK=%s GW=%s (lease=%us)\n",
                                   client->dev->name, ip_str, mask_str, gw_str,
                                   client->lease_time);
            
            if (client->callback) {
                client->callback(client, true);
            }
            break;
        }
            
        case DHCP_NAK: {
            terminal_error_printf("[DHCP] %s: NAK received\n", client->dev->name);
            client->configured = false;
            if (client->callback) {
                client->callback(client, false);
            }
            break;
        }
    }
}

/*
 * =============================================================================== Incoming DHCP packet handler ================================================================================
 */

void dhcp_input(net_buf_t *buf, ipv4_addr_t src, ipv4_addr_t dst,
                u16 src_port, u16 dst_port) {
    (void)src;
    (void)dst;
    (void)src_port;
    
    if (buf->len < sizeof(dhcp_header_t)) return;
    
    dhcp_header_t *dhcp = (dhcp_header_t*)buf->data;
    
    if (ntohl(dhcp->magic) != DHCP_MAGIC_COOKIE) return;
    
    /*
 * Looking for a client by MAC address
 */
    dhcp_client_entry_t *entry = g_dhcp_clients;
    while (entry) {
        if (memcmp(entry->client.dev->mac_addr, dhcp->chaddr, 6) == 0) {
            dhcp_process_response(&entry->client, dhcp);
            break;
        }
        entry = entry->next;
    }
}

/*
 * =============================================================================== DHCP client API (universal) ===============================================================================
 */

int dhcp_init(dhcp_client_t *client, net_device_t *dev) {
    if (!client || !dev) return -1;
    
    memset(client, 0, sizeof(dhcp_client_t));
    client->dev = dev;
    client->xid = dhcp_generate_xid();
    
    /*
 * Add to the global list
 */
    dhcp_client_entry_t *entry = (dhcp_client_entry_t*)malloc(sizeof(dhcp_client_entry_t));
    if (!entry) return -1;
    
    memcpy(&entry->client, client, sizeof(dhcp_client_t));
    entry->next = g_dhcp_clients;
    g_dhcp_clients = entry;
    g_dhcp_clients_count++;
    
    terminal_printf("[DHCP] Client initialized for %s (%s)\n", dev->name,
                   dev->type == NET_TYPE_ETHERNET ? "Ethernet" :
                   dev->type == NET_TYPE_WIFI ? "WiFi" : "Unknown");
    
    return 0;
}

int dhcp_start(dhcp_client_t *client) {
    if (!client) return -1;
    
    client->running = true;
    client->configured = false;
    client->xid = dhcp_generate_xid();
    
    return dhcp_send_message(client, DHCP_DISCOVER, 0);
}

int dhcp_renew(dhcp_client_t *client) {
    if (!client || !client->configured) return -1;
    
    client->xid = dhcp_generate_xid();
    return dhcp_send_message(client, DHCP_REQUEST, client->yiaddr);
}

int dhcp_release(dhcp_client_t *client) {
    if (!client || !client->configured) return -1;
    
    dhcp_send_message(client, DHCP_RELEASE, client->yiaddr);
    client->configured = false;
    
    return 0;
}

/*
 * ============================================================================== Start DHCP on all interfaces ===============================================================================
 */

void dhcp_start_on_all_interfaces(dhcp_callback_t callback) {
    list_head_t *pos;
    int started = 0;
    
    terminal_printf("[DHCP] Starting on all active interfaces...\n");
    
    list_for_each(pos, &net_devices) {
        net_device_t *dev = list_entry(pos, net_device_t, node);
        
        /*
 * We configure only interfaces that can work with DHCP
 */
        if (dev->type == NET_TYPE_ETHERNET || dev->type == NET_TYPE_WIFI) {
            if (dev->up && dev->running) {
                dhcp_client_t *client = (dhcp_client_t*)malloc(sizeof(dhcp_client_t));
                if (client) {
                    dhcp_init(client, dev);
                    client->callback = callback;
                    dhcp_start(client);
                    started++;
                }
            }
        }
    }
    
    terminal_printf("[DHCP] Started on %d interface(s)\n", started);
}

/*
 * =============================================================================== Initializing the DHCP subsystem ==============================================================================
 */

void dhcp_subsystem_init(void) {
    g_dhcp_clients = NULL;
    g_dhcp_clients_count = 0;
    
    udp_register_handler(DHCP_CLIENT_PORT, dhcp_input);
    
    terminal_printf("[DHCP] Subsystem initialized\n");
}
