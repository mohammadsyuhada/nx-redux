/*
 * NXRedux Network Common Module
 * Shared networking utilities for netplay and gbalink
 */

#include "network_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h> // IFF_UP / IFF_LOOPBACK for NET_getLanInfo

#include "defines.h" // IWYU pragma: keep — HAS_RUNTIME_PATHS (desktop) gates the directed broadcast

// Default TCP configuration
static const NET_TCPConfig DEFAULT_TCP_CONFIG = {
	.buffer_size = 65536, // 64KB
	.recv_timeout_us = 0, // No timeout
	.enable_keepalive = false};

// Character set for SSID generation (excludes confusing chars: 0/O, 1/I)
static const char* SSID_CHARSET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static const int SSID_CHARSET_LEN = 32;

//////////////////////////////////////////////////////////////////////////////
// IP Address Utilities
//////////////////////////////////////////////////////////////////////////////

// Interface-selection core behind NET_getLocalIP and the desktop arm of
// NET_sendDiscoveryBroadcast. Skips loopback by FLAG, not name — macOS calls
// it lo0, which the old strcmp("lo") let straight through — plus interfaces
// that are down and the virtual links a desktop host carries (VPN tunnels,
// VM/container bridges, Apple peer-to-peer). Advertising one of those
// addresses sends the peer's connect somewhere unreachable; same ruling and
// same prefix list as Device Sync's get_own_ip (sync.c). wlan* still wins
// outright so a device with a second link keeps advertising the radio;
// otherwise the FIRST real interface is kept (the old loop kept the LAST,
// which on a Mac with a VM bridge was the NAT bridge).
//
// ip_out and bcast_out are each optional; bcast_out gets the subnet-directed
// broadcast (ip | ~mask) of the selected interface. Returns 0 when a usable
// interface was found, -1 otherwise (outputs untouched).
int NET_getLanInfo(char* ip_out, size_t ip_size, char* bcast_out, size_t bcast_size) {
	static const char* virtual_prefixes[] = {
		"utun", "bridge", "awdl", "llw", "vmnet", "docker",
		"veth", "tap", "tun", "zt", "p2p", NULL};

	struct ifaddrs* ifaddr;
	if (getifaddrs(&ifaddr) == -1)
		return -1;

	int found = -1;
	for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK))
			continue;

		bool is_virtual = false;
		for (int v = 0; virtual_prefixes[v]; v++) {
			if (strncmp(ifa->ifa_name, virtual_prefixes[v],
						strlen(virtual_prefixes[v])) == 0) {
				is_virtual = true;
				break;
			}
		}
		if (is_virtual)
			continue;

		bool is_wlan = (strncmp(ifa->ifa_name, "wlan", 4) == 0);
		if (found == 0 && !is_wlan)
			continue; // already holding the first real interface

		struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
		if (ip_out && ip_size >= 16)
			inet_ntop(AF_INET, &addr->sin_addr, ip_out, ip_size);

		if (bcast_out && bcast_size >= 16 && ifa->ifa_netmask) {
			struct sockaddr_in* mask = (struct sockaddr_in*)ifa->ifa_netmask;
			struct in_addr bcast_addr;
			bcast_addr.s_addr = addr->sin_addr.s_addr | ~mask->sin_addr.s_addr;
			inet_ntop(AF_INET, &bcast_addr, bcast_out, bcast_size);
		}

		found = 0;
		if (is_wlan)
			break;
	}

	freeifaddrs(ifaddr);
	return found;
}

void NET_getLocalIP(char* ip_out, size_t ip_size) {
	if (!ip_out || ip_size < 16)
		return;

	strncpy(ip_out, "0.0.0.0", ip_size - 1);
	ip_out[ip_size - 1] = '\0';

	NET_getLanInfo(ip_out, ip_size, NULL, 0);
}

bool NET_hasConnection(void) {
	char ip[16];
	NET_getLocalIP(ip, sizeof(ip));
	return strcmp(ip, "0.0.0.0") != 0;
}

//////////////////////////////////////////////////////////////////////////////
// TCP Socket Configuration
//////////////////////////////////////////////////////////////////////////////

void NET_configureTCPSocket(int fd, const NET_TCPConfig* config) {
	if (fd < 0)
		return;

	const NET_TCPConfig* cfg = config ? config : &DEFAULT_TCP_CONFIG;

	// Enable TCP_NODELAY to disable Nagle's algorithm
	int opt = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

	// Set socket buffer sizes
	if (cfg->buffer_size > 0) {
		setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &cfg->buffer_size, sizeof(cfg->buffer_size));
		setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &cfg->buffer_size, sizeof(cfg->buffer_size));
	}

	// Set receive timeout if specified
	if (cfg->recv_timeout_us > 0) {
		struct timeval tv = {
			.tv_sec = cfg->recv_timeout_us / 1000000,
			.tv_usec = cfg->recv_timeout_us % 1000000};
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	}

	// Enable keepalive if requested
	if (cfg->enable_keepalive) {
		int keepalive = 1;
		setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
	}
}

//////////////////////////////////////////////////////////////////////////////
// Server Socket Creation
//////////////////////////////////////////////////////////////////////////////

int NET_createListenSocket(uint16_t port, char* error_msg, size_t error_size) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		if (error_msg && error_size > 0) {
			snprintf(error_msg, error_size, "Socket creation failed");
		}
		return -1;
	}

	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(fd);
		if (error_msg && error_size > 0) {
			snprintf(error_msg, error_size, "Bind failed on port %d", port);
		}
		return -1;
	}

	if (listen(fd, 1) < 0) {
		close(fd);
		if (error_msg && error_size > 0) {
			snprintf(error_msg, error_size, "Listen failed");
		}
		return -1;
	}

	return fd;
}

int NET_createBroadcastSocket(void) {
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd >= 0) {
		int broadcast = 1;
		setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
	}
	return fd;
}

void NET_ensureBroadcastSocket(int* udp_fd, char* status_msg, size_t msg_size) {
	if (!udp_fd || *udp_fd >= 0)
		return; // Already running

	*udp_fd = NET_createBroadcastSocket();
	if (*udp_fd < 0 && status_msg && msg_size > 0) {
		snprintf(status_msg, msg_size, "Failed to restart broadcast");
	}
}

int NET_createDiscoveryListenSocket(uint16_t port) {
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;

	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}

	// Set non-blocking
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	return fd;
}

//////////////////////////////////////////////////////////////////////////////
// Hotspot Utilities
//////////////////////////////////////////////////////////////////////////////

void NET_generateHotspotSSID(char* ssid_out, size_t ssid_size, const NET_HotspotConfig* config) {
	if (!ssid_out || ssid_size < 16 || !config || !config->prefix)
		return;

	// Seed random with provided seed
	srand(config->seed);

	// Generate 4-character random code
	char code[5];
	for (int i = 0; i < 4; i++) {
		code[i] = SSID_CHARSET[rand() % SSID_CHARSET_LEN];
	}
	code[4] = '\0';

	snprintf(ssid_out, ssid_size, "%s%s", config->prefix, code);
}

//////////////////////////////////////////////////////////////////////////////
// Broadcast Timer
//////////////////////////////////////////////////////////////////////////////

void NET_initBroadcastTimer(NET_BroadcastTimer* timer, int interval_us) {
	if (!timer)
		return;
	timer->last_broadcast.tv_sec = 0;
	timer->last_broadcast.tv_usec = 0;
	timer->interval_us = interval_us;
}

bool NET_shouldBroadcast(NET_BroadcastTimer* timer) {
	if (!timer)
		return false;

	struct timeval now;
	gettimeofday(&now, NULL);

	long elapsed_us = (now.tv_sec - timer->last_broadcast.tv_sec) * 1000000 +
					  (now.tv_usec - timer->last_broadcast.tv_usec);

	if (elapsed_us >= timer->interval_us) {
		timer->last_broadcast = now;
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////
// Discovery Utilities
//////////////////////////////////////////////////////////////////////////////

void NET_sendDiscoveryBroadcast(int udp_fd, uint32_t magic, uint32_t protocol_version,
								uint32_t game_crc, uint16_t tcp_port,
								uint16_t discovery_port, const char* game_name,
								const char* link_mode) {
	if (udp_fd < 0)
		return;

	NET_DiscoveryPacket pkt = {0};
	pkt.magic = htonl(magic);
	pkt.protocol_version = htonl(protocol_version);
	pkt.game_crc = htonl(game_crc);
	pkt.port = htons(tcp_port);
	if (game_name) {
		strncpy(pkt.game_name, game_name, NET_MAX_GAME_NAME - 1);
	}
	if (link_mode) {
		strncpy(pkt.link_mode, link_mode, NET_MAX_LINK_MODE - 1);
	}

	struct sockaddr_in bcast = {0};
	bcast.sin_family = AF_INET;
	bcast.sin_addr.s_addr = INADDR_BROADCAST;
	bcast.sin_port = htons(discovery_port);

#if defined(HAS_RUNTIME_PATHS)
	// Desktop: 255.255.255.255 leaves on whichever interface holds the
	// default route — with a VPN up that is the tunnel, and the LAN never
	// hears the packet. The subnet-directed address of the interface whose
	// IP the host advertises reaches the same peers over the right link.
	// Devices keep the limited broadcast: single radio, and the hotspot path
	// sends before the AP subnet is even settled.
	char bcast_ip[16];
	if (NET_getLanInfo(NULL, 0, bcast_ip, sizeof(bcast_ip)) == 0)
		inet_pton(AF_INET, bcast_ip, &bcast.sin_addr);
#endif

	sendto(udp_fd, &pkt, sizeof(pkt), 0,
		   (struct sockaddr*)&bcast, sizeof(bcast));
}

int NET_receiveDiscoveryResponses(int udp_fd, uint32_t expected_magic,
								  NET_HostInfo* hosts, int* current_count,
								  int max_hosts) {
	if (udp_fd < 0 || !hosts || !current_count)
		return 0;

	NET_DiscoveryPacket pkt;
	struct sockaddr_in sender;
	socklen_t sender_len = sizeof(sender);

	while (recvfrom(udp_fd, &pkt, sizeof(pkt), MSG_DONTWAIT,
					(struct sockaddr*)&sender, &sender_len) == sizeof(pkt)) {
		if (ntohl(pkt.magic) != expected_magic)
			continue;

		char ip[16];
		inet_ntop(AF_INET, &sender.sin_addr, ip, sizeof(ip));

		// Check for duplicates
		bool found = false;
		for (int i = 0; i < *current_count; i++) {
			if (strcmp(hosts[i].host_ip, ip) == 0) {
				found = true;
				break;
			}
		}

		if (!found && *current_count < max_hosts) {
			NET_HostInfo* h = &hosts[*current_count];
			strncpy(h->game_name, pkt.game_name, NET_MAX_GAME_NAME - 1);
			h->game_name[NET_MAX_GAME_NAME - 1] = '\0';
			strncpy(h->host_ip, ip, sizeof(h->host_ip) - 1);
			h->host_ip[sizeof(h->host_ip) - 1] = '\0';
			h->port = ntohs(pkt.port);
			h->game_crc = ntohl(pkt.game_crc);
			strncpy(h->link_mode, pkt.link_mode, NET_MAX_LINK_MODE - 1);
			h->link_mode[NET_MAX_LINK_MODE - 1] = '\0';
			(*current_count)++;
		}

		sender_len = sizeof(sender); // Reset for next iteration
	}

	return *current_count;
}
