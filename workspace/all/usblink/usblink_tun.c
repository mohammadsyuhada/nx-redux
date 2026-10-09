#define _GNU_SOURCE
#include "usblink_tun.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define TUN_DEV "/dev/net/tun"
#define TUN_NETMASK "255.255.255.252"
#define TUN_MTU 1500

// The stock tg5050 kernel has no tun built in, so we ship tun.ko. finit_module
// is called directly (both 4.9 and 5.15 have it) rather than relying on the
// device's busybox insmod.
int usblink_tun_ensure_module(const char* ko_path) {
	if (access(TUN_DEV, F_OK) == 0)
		return 0;
	int fd = open(ko_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "usblink: open %s: %s\n", ko_path, strerror(errno));
		return -1;
	}
	int r = (int)syscall(__NR_finit_module, fd, "", 0);
	int err = errno;
	close(fd);
	if (r != 0 && err != EEXIST) {
		fprintf(stderr, "usblink: finit_module %s: %s\n", ko_path, strerror(err));
		return -1;
	}
	// devtmpfs creates the node asynchronously after the misc device registers.
	for (int i = 0; i < 100; i++) {
		if (access(TUN_DEV, F_OK) == 0)
			return 0;
		struct timespec ts = {0, 10 * 1000 * 1000};
		nanosleep(&ts, NULL);
	}
	fprintf(stderr, "usblink: %s did not appear\n", TUN_DEV);
	return -1;
}

int usblink_tun_open(const char* name) {
	int fd = open(TUN_DEV, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "usblink: open %s: %s\n", TUN_DEV, strerror(errno));
		return -1;
	}
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI; // raw IP packets, no 4-byte packet-info prefix
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
	if (ioctl(fd, TUNSETIFF, &ifr) != 0) {
		fprintf(stderr, "usblink: TUNSETIFF %s: %s\n", name, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

static int set_addr(int sock, const char* name, unsigned long req, const char* ip) {
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
	struct sockaddr_in* sin = (struct sockaddr_in*)&ifr.ifr_addr;
	sin->sin_family = AF_INET;
	if (inet_pton(AF_INET, ip, &sin->sin_addr) != 1) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(sock, req, &ifr);
}

static int change_flags(int sock, const char* name, short set, short clear) {
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
	if (ioctl(sock, SIOCGIFFLAGS, &ifr) != 0)
		return -1;
	ifr.ifr_flags = (short)((ifr.ifr_flags | set) & ~clear);
	return ioctl(sock, SIOCSIFFLAGS, &ifr);
}

int usblink_tun_set_up(const char* name, const char* local_ip, const char* peer_ip) {
	int sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (sock < 0)
		return -1;
	int rc = -1;
	const char* step = "SIOCSIFADDR";
	if (set_addr(sock, name, SIOCSIFADDR, local_ip) != 0)
		goto out;
	step = "SIOCSIFDSTADDR";
	if (set_addr(sock, name, SIOCSIFDSTADDR, peer_ip) != 0)
		goto out;
	step = "SIOCSIFNETMASK";
	if (set_addr(sock, name, SIOCSIFNETMASK, TUN_NETMASK) != 0)
		goto out;
	step = "SIOCSIFMTU";
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
	ifr.ifr_mtu = TUN_MTU;
	if (ioctl(sock, SIOCSIFMTU, &ifr) != 0)
		goto out;
	step = "SIOCSIFFLAGS";
	if (change_flags(sock, name, IFF_UP | IFF_RUNNING, 0) != 0)
		goto out;
	rc = 0;
out:
	if (rc != 0)
		fprintf(stderr, "usblink: %s %s: %s\n", step, name, strerror(errno));
	close(sock);
	return rc;
}

// Dropping the address removes the 10.99.0.0/30 route, so netplay discovery
// stops seeing the peer while the cable is out.
int usblink_tun_set_down(const char* name) {
	int sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (sock < 0)
		return -1;
	int rc = 0;
	if (change_flags(sock, name, 0, IFF_UP) != 0)
		rc = -1;
	// Setting 0.0.0.0 deletes the interface's IPv4 address. EADDRNOTAVAIL just
	// means there was none, which is the state we want.
	if (set_addr(sock, name, SIOCSIFADDR, "0.0.0.0") != 0 && errno != EADDRNOTAVAIL)
		rc = -1;
	if (rc != 0)
		fprintf(stderr, "usblink: tun down %s: %s\n", name, strerror(errno));
	close(sock);
	return rc;
}
