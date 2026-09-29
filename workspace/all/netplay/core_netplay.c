// core_netplay.c - see core_netplay.h.

#include "core_netplay.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int active = 0;
static int ended = 0;
static char peer_ip[64];
static int bye_fd = -1;

void CoreNetplay_initFromEnv(void) {
	const char* v = getenv("NX_CORE_NETPLAY");
	active = v && strcmp(v, "1") == 0;
	ended = 0;
	const char* peer = getenv("NX_GGPO_SERVER");
	snprintf(peer_ip, sizeof(peer_ip), "%s", peer ? peer : "");
	if (active) {
		char ms[16];
		snprintf(ms, sizeof(ms), "%d", CORE_NETPLAY_DISCONNECT_MS);
		setenv("NX_GGPO_DISCONNECT_MS", ms, 1);
	}
}

int CoreNetplay_isActive(void) {
	return active && !ended;
}

void CoreNetplay_markEnded(void) {
	if (active)
		ended = 1;
}

int CoreNetplay_hasEnded(void) {
	return ended;
}

int CoreNetplay_leaveSecondsLeft(uint32_t start_ms, uint32_t now_ms) {
	uint32_t elapsed = now_ms - start_ms; // unsigned: wraparound safe
	if (elapsed >= CORE_NETPLAY_LEAVE_GRACE_MS)
		return 0;
	return (int)((CORE_NETPLAY_LEAVE_GRACE_MS - elapsed + 999) / 1000);
}

int CoreNetplay_byeOpen(uint16_t port) {
	CoreNetplay_byeClose();
	if (!active || !peer_ip[0])
		return -1;
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) != 0) {
		close(fd);
		return -1;
	}
	bye_fd = fd;
	return 0;
}

void CoreNetplay_byeSend(uint16_t port) {
	if (bye_fd < 0 || !peer_ip[0])
		return;
	struct sockaddr_in to = {0};
	to.sin_family = AF_INET;
	to.sin_port = htons(port);
	if (inet_pton(AF_INET, peer_ip, &to.sin_addr) != 1)
		return;
	// UDP can drop one; a few copies cost nothing
	for (int i = 0; i < 3; i++)
		sendto(bye_fd, CORE_NETPLAY_BYE_MSG, strlen(CORE_NETPLAY_BYE_MSG), 0, (struct sockaddr*)&to, sizeof(to));
}

int CoreNetplay_byePoll(void) {
	if (bye_fd < 0)
		return 0;
	struct in_addr peer;
	if (inet_pton(AF_INET, peer_ip, &peer) != 1)
		return 0;
	int bye = 0;
	char buf[64];
	struct sockaddr_in from;
	socklen_t from_len = sizeof(from);
	ssize_t n;
	while ((n = recvfrom(bye_fd, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &from_len)) >= 0) {
		buf[n] = '\0';
		if (from.sin_addr.s_addr == peer.s_addr && strcmp(buf, CORE_NETPLAY_BYE_MSG) == 0)
			bye = 1;
		from_len = sizeof(from);
	}
	return bye;
}

void CoreNetplay_byeClose(void) {
	if (bye_fd >= 0)
		close(bye_fd);
	bye_fd = -1;
}
