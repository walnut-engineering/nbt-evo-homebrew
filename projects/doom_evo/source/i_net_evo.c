/*
 * DOOM EVO Network Implementation
 * 
 * This implementation provides full IPv4 networking support for QNX 6.6
 * using proper QNX networking stack and toolchain headers.
 * 
 * Key features:
 * - Full IPv4 UDP networking support
 * - QNX-specific socket optimizations
 * - Network interface discovery and configuration
 * - Proper error handling and recovery
 * - Support for both single-player and multiplayer modes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>

// DOOM includes
#include "doomdef.h"
#include "d_event.h"
#include "d_net.h"
#include "m_argv.h"
#include "doomstat.h"
#include "i_system.h"
#include "i_net.h"

// Network configuration
#define DOOMPORT_DEFAULT    5029
#define MAX_PACKET_SIZE     1400
#define NETWORK_BUFFER_SIZE 8192
#define MAX_NETWORK_NODES   8

// QNX-specific networking context
typedef struct {
    int initialized;
    int debug_enabled;
    
    // Socket configuration
    int send_socket;
    int recv_socket;
    
    // Network interface information
    char primary_interface[IFNAMSIZ];
    struct sockaddr_in local_addr;
    struct sockaddr_in broadcast_addr;
    
    // Packet handling
    unsigned char send_buffer[NETWORK_BUFFER_SIZE];
    unsigned char recv_buffer[NETWORK_BUFFER_SIZE];
    
    // Statistics
    int packets_sent;
    int packets_received;
    int network_errors;
} qnx_network_context_t;

static qnx_network_context_t g_net_ctx = {
    .initialized = 0,
    .debug_enabled = 1,  // Enable debug for network diagnostics
    .send_socket = -1,
    .recv_socket = -1,
    .packets_sent = 0,
    .packets_received = 0,
    .network_errors = 0
};

// External variables from DOOM networking
extern doomcom_t    *doomcom;
extern doomdata_t   *netbuffer;

// Network function pointers (required by DOOM engine)
void (*netget)(void);
void (*netsend)(void);

// DOOM port (can be overridden with -port parameter)
static int DOOMPORT = DOOMPORT_DEFAULT;

// Socket addresses for communication
static struct sockaddr_in sendaddress[MAXNETNODES];

//
// QNX Network Interface Discovery
//
static int discover_network_interface(void) {
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    int found_interface = 0;
    
    if (getifaddrs(&ifap) == -1) {
        printf("NET: Failed to get network interfaces: %s\n", strerror(errno));
        return 0;
    }
    
    printf("NET: Discovering network interfaces...\n");
    
    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        // Look for IPv4 interfaces
        if (ifa->ifa_addr->sa_family == AF_INET) {
            sa = (struct sockaddr_in*)ifa->ifa_addr;
            char addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa->sin_addr, addr_str, INET_ADDRSTRLEN);
            
            printf("NET: Found interface %s: %s\n", ifa->ifa_name, addr_str);
            
            // Skip loopback interface
            if (ifa->ifa_flags & IFF_LOOPBACK) {
                printf("NET: Skipping loopback interface %s\n", ifa->ifa_name);
                continue;
            }
            
            // Skip interfaces that are down
            if (!(ifa->ifa_flags & IFF_UP)) {
                printf("NET: Skipping down interface %s\n", ifa->ifa_name);
                continue;
            }
            
            // Prefer the interface in the same subnet as our SSH connection (169.254.x.x)
            if (ntohl(sa->sin_addr.s_addr) >> 16 == 0xA9FE) {  // 169.254.x.x
                printf("NET: Using link-local interface %s: %s\n", ifa->ifa_name, addr_str);
                strncpy(g_net_ctx.primary_interface, ifa->ifa_name, IFNAMSIZ-1);
                g_net_ctx.primary_interface[IFNAMSIZ-1] = '\0';
                memcpy(&g_net_ctx.local_addr, sa, sizeof(struct sockaddr_in));
                found_interface = 1;
                break;
            }
            
            // Fallback to first non-loopback interface if no link-local found
            if (!found_interface && !(ifa->ifa_flags & IFF_LOOPBACK)) {
                printf("NET: Using fallback interface %s: %s\n", ifa->ifa_name, addr_str);
                strncpy(g_net_ctx.primary_interface, ifa->ifa_name, IFNAMSIZ-1);
                g_net_ctx.primary_interface[IFNAMSIZ-1] = '\0';
                memcpy(&g_net_ctx.local_addr, sa, sizeof(struct sockaddr_in));
                found_interface = 1;
            }
        }
    }
    
    freeifaddrs(ifap);
    
    if (found_interface) {
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &g_net_ctx.local_addr.sin_addr, addr_str, INET_ADDRSTRLEN);
        printf("NET: Selected interface %s with address %s\n", 
               g_net_ctx.primary_interface, addr_str);
        
        // Calculate broadcast address
        g_net_ctx.broadcast_addr = g_net_ctx.local_addr;
        g_net_ctx.broadcast_addr.sin_addr.s_addr |= htonl(0xFF);  // Simple /24 broadcast
        
        return 1;
    }
    
    printf("NET: No suitable network interface found\n");
    return 0;
}

//
// UDPsocket - Create a UDP socket
//
int UDPsocket(void) {
    int sock;
    int opt = 1;
    
    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        printf("NET: Failed to create UDP socket: %s\n", strerror(errno));
        g_net_ctx.network_errors++;
        return -1;
    }
    
    // Enable address reuse
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("NET: Warning - failed to set SO_REUSEADDR: %s\n", strerror(errno));
    }
    
    // Enable broadcast for multiplayer discovery
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        printf("NET: Warning - failed to set SO_BROADCAST: %s\n", strerror(errno));
    }
    
    // Set non-blocking mode
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }
    
    printf("NET: Created UDP socket %d\n", sock);
    return sock;
}

//
// BindToLocalPort - Bind socket to specific port
//
static void BindToLocalPort(int socket, int port) {
    struct sockaddr_in address;
    
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = g_net_ctx.local_addr.sin_addr.s_addr;
    address.sin_port = htons(port);
    
    if (bind(socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        printf("NET: Failed to bind socket to port %d: %s\n", port, strerror(errno));
        g_net_ctx.network_errors++;
        I_Error("Unable to bind to port %d", port);
    }
    
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &address.sin_addr, addr_str, INET_ADDRSTRLEN);
    printf("NET: Bound socket to %s:%d\n", addr_str, port);
}

//
// PacketSend - Send a network packet
//
void PacketSend(void) {
    int c;
    doomdata_t sw;
    
    // Copy packet data and convert to network byte order
    sw.checksum = htonl(netbuffer->checksum);
    sw.player = netbuffer->player;
    sw.retransmitfrom = netbuffer->retransmitfrom;
    sw.starttic = netbuffer->starttic;
    sw.numtics = netbuffer->numtics;
    
    for (c = 0; c < netbuffer->numtics; c++) {
        sw.cmds[c].forwardmove = netbuffer->cmds[c].forwardmove;
        sw.cmds[c].sidemove = netbuffer->cmds[c].sidemove;
        sw.cmds[c].angleturn = htons(netbuffer->cmds[c].angleturn);
        sw.cmds[c].consistancy = htons(netbuffer->cmds[c].consistancy);
        sw.cmds[c].chatchar = netbuffer->cmds[c].chatchar;
        sw.cmds[c].buttons = netbuffer->cmds[c].buttons;
    }
    
    // Calculate packet size
    int packet_size = (int)&(((doomdata_t *)0)->cmds[netbuffer->numtics]);
    
    if (g_net_ctx.debug_enabled && g_net_ctx.packets_sent < 10) {
        printf("NET: Sending packet %d bytes to node %d (player %d, tics %d)\n", 
               packet_size, doomcom->remotenode, netbuffer->player, netbuffer->numtics);
    }
    
    // Send to destination node
    ssize_t bytes_sent = sendto(g_net_ctx.send_socket, &sw, packet_size, 0,
                               (struct sockaddr *)&sendaddress[doomcom->remotenode],
                               sizeof(sendaddress[doomcom->remotenode]));
    
    if (bytes_sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("NET: Send error: %s\n", strerror(errno));
            g_net_ctx.network_errors++;
        }
    } else {
        g_net_ctx.packets_sent++;
        if (g_net_ctx.debug_enabled && (g_net_ctx.packets_sent % 100) == 0) {
            printf("NET: Sent %d packets (errors: %d)\n", 
                   g_net_ctx.packets_sent, g_net_ctx.network_errors);
        }
    }
}

//
// PacketGet - Receive a network packet
//
void PacketGet(void) {
    int c;
    struct sockaddr_in fromaddress;
    socklen_t fromlen;
    doomdata_t sw;
    
    fromlen = sizeof(fromaddress);
    ssize_t bytes_received = recvfrom(g_net_ctx.recv_socket, &sw, sizeof(sw), 0,
                                     (struct sockaddr *)&fromaddress, &fromlen);
    
    if (bytes_received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("NET: Receive error: %s\n", strerror(errno));
            g_net_ctx.network_errors++;
        }
        doomcom->remotenode = -1;  // No packet received
        return;
    }
    
    g_net_ctx.packets_received++;
    
    if (g_net_ctx.debug_enabled && g_net_ctx.packets_received < 10) {
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &fromaddress.sin_addr, addr_str, INET_ADDRSTRLEN);
        printf("NET: Received packet %zd bytes from %s:%d\n", 
               bytes_received, addr_str, ntohs(fromaddress.sin_port));
    }
    
    // Find which node this packet came from
    doomcom->remotenode = -1;
    for (c = 0; c < doomcom->numnodes; c++) {
        if (fromaddress.sin_addr.s_addr == sendaddress[c].sin_addr.s_addr) {
            doomcom->remotenode = c;
            break;
        }
    }
    
    if (doomcom->remotenode == -1) {
        if (g_net_ctx.debug_enabled) {
            char addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &fromaddress.sin_addr, addr_str, INET_ADDRSTRLEN);
            printf("NET: Packet from unknown node %s:%d\n", 
                   addr_str, ntohs(fromaddress.sin_port));
        }
        return;
    }
    
    // Convert from network byte order
    netbuffer->checksum = ntohl(sw.checksum);
    netbuffer->player = sw.player;
    netbuffer->retransmitfrom = sw.retransmitfrom;
    netbuffer->starttic = sw.starttic;
    netbuffer->numtics = sw.numtics;
    
    for (c = 0; c < netbuffer->numtics; c++) {
        netbuffer->cmds[c].forwardmove = sw.cmds[c].forwardmove;
        netbuffer->cmds[c].sidemove = sw.cmds[c].sidemove;
        netbuffer->cmds[c].angleturn = ntohs(sw.cmds[c].angleturn);
        netbuffer->cmds[c].consistancy = ntohs(sw.cmds[c].consistancy);
        netbuffer->cmds[c].chatchar = sw.cmds[c].chatchar;
        netbuffer->cmds[c].buttons = sw.cmds[c].buttons;
    }
}

//
// I_InitNetwork - Initialize the network subsystem
//
void I_InitNetwork(void) {
    int i, p;
    char *v;
    
    printf("NET: Initializing DOOM EVO network subsystem...\n");
    
    // Check if already initialized
    if (g_net_ctx.initialized) {
        printf("NET: Network already initialized\n");
        return;
    }
    
    // Allocate doomcom structure (CRITICAL - was missing!)
    doomcom = malloc(sizeof(*doomcom));
    if (!doomcom) {
        I_Error("Failed to allocate doomcom structure");
    }
    memset(doomcom, 0, sizeof(*doomcom));
    
    // Set up ticdup parameter
    i = M_CheckParm("-dup");
    if (i && i < myargc - 1) {
        doomcom->ticdup = myargv[i + 1][0] - '0';
        if (doomcom->ticdup < 1) {
            doomcom->ticdup = 1;
        }
        if (doomcom->ticdup > 9) {
            doomcom->ticdup = 9;
        }
    } else {
        doomcom->ticdup = 1;
    }
    
    // Set up extratics parameter
    if (M_CheckParm("-extratic")) {
        doomcom->extratics = 1;
    } else {
        doomcom->extratics = 0;
    }
    
    // Discover and configure network interface
    if (!discover_network_interface()) {
        // Fallback to single-player mode
        printf("NET: No network interface available, forcing single-player mode\n");
        netgame = false;
        doomcom->id = DOOMCOM_ID;
        doomcom->numplayers = doomcom->numnodes = 1;
        doomcom->deathmatch = false;
        doomcom->consoleplayer = 0;
        g_net_ctx.initialized = 1;
        return;
    }
    
    // Check for port override
    p = M_CheckParm("-port");
    if (p && p < myargc - 1) {
        DOOMPORT = atoi(myargv[p + 1]);
        printf("NET: Using custom port %d\n", DOOMPORT);
    } else {
        printf("NET: Using default port %d\n", DOOMPORT);
    }
    
    // Parse network game options: -net <consoleplayer> <host> <host> ...
    i = M_CheckParm("-net");
    if (!i) {
        // Single player mode
        printf("NET: Single-player mode initialized\n");
        netgame = false;
        doomcom->id = DOOMCOM_ID;
        doomcom->numplayers = doomcom->numnodes = 1;
        doomcom->deathmatch = false;
        doomcom->consoleplayer = 0;
        g_net_ctx.initialized = 1;
        return;
    }
    
    // Network multiplayer mode
    printf("NET: Initializing multiplayer network mode...\n");
    
    netsend = PacketSend;
    netget = PacketGet;
    netgame = true;
    
    // Parse player number and host list
    doomcom->consoleplayer = myargv[i + 1][0] - '1';
    doomcom->numnodes = 1;  // This node for sure
    
    printf("NET: Console player %d\n", doomcom->consoleplayer);
    
    // Create sockets
    g_net_ctx.recv_socket = UDPsocket();
    if (g_net_ctx.recv_socket < 0) {
        I_Error("Failed to create receive socket");
    }
    
    g_net_ctx.send_socket = UDPsocket();
    if (g_net_ctx.send_socket < 0) {
        I_Error("Failed to create send socket");
    }
    
    // Bind receive socket to our port
    BindToLocalPort(g_net_ctx.recv_socket, DOOMPORT);
    
    // Set up addresses for other nodes
    i++;
    while (++i < myargc && myargv[i][0] != '-') {
        struct hostent *hostentry;
        struct sockaddr_in *addr = &sendaddress[doomcom->numnodes];
        
        memset(addr, 0, sizeof(*addr));
        addr->sin_family = AF_INET;
        addr->sin_port = htons(DOOMPORT);
        
        // Try to parse as IP address first
        if (inet_aton(myargv[i], &addr->sin_addr) == 0) {
            // Not an IP address, try hostname lookup
            hostentry = gethostbyname(myargv[i]);
            if (!hostentry) {
                printf("NET: Failed to resolve hostname %s\n", myargv[i]);
                I_Error("gethostbyname: couldn't resolve %s", myargv[i]);
            }
            addr->sin_addr = *(struct in_addr *)hostentry->h_addr_list[0];
        }
        
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, addr_str, INET_ADDRSTRLEN);
        printf("NET: Node %d: %s:%d\n", doomcom->numnodes, addr_str, DOOMPORT);
        
        doomcom->numnodes++;
        if (doomcom->numnodes >= MAXNETNODES) {
            printf("NET: Maximum number of nodes reached (%d)\n", MAXNETNODES);
            break;
        }
    }
    
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes;
    
    // Check for deathmatch
    if (M_CheckParm("-deathmatch")) {
        doomcom->deathmatch = 1;
        printf("NET: Deathmatch mode enabled\n");
    } else {
        doomcom->deathmatch = 0;
    }
    
    printf("NET: Network initialized - %d nodes, console player %d\n", 
           doomcom->numnodes, doomcom->consoleplayer);
    
    g_net_ctx.initialized = 1;
}

//
// I_NetCmd - Handle network commands (if any)
//
void I_NetCmd(void) {
    // This can be used for network-specific console commands
    // Currently not implemented in original DOOM
    if (g_net_ctx.debug_enabled && (g_net_ctx.packets_sent + g_net_ctx.packets_received) > 0) {
        printf("NET: Stats - Sent: %d, Received: %d, Errors: %d\n",
               g_net_ctx.packets_sent, g_net_ctx.packets_received, g_net_ctx.network_errors);
    }
}

//
// I_ShutdownNetwork - Clean up network resources
//
void I_ShutdownNetwork(void) {
    if (!g_net_ctx.initialized) {
        return;
    }
    
    printf("NET: Shutting down network subsystem...\n");
    
    if (g_net_ctx.send_socket >= 0) {
        close(g_net_ctx.send_socket);
        g_net_ctx.send_socket = -1;
    }
    
    if (g_net_ctx.recv_socket >= 0) {
        close(g_net_ctx.recv_socket);
        g_net_ctx.recv_socket = -1;
    }
    
    printf("NET: Network shutdown complete - Sent: %d, Received: %d, Errors: %d\n",
           g_net_ctx.packets_sent, g_net_ctx.packets_received, g_net_ctx.network_errors);
    
    g_net_ctx.initialized = 0;
}
