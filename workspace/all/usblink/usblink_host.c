#define _GNU_SOURCE
#include "usblink_host.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define USB_DEVICES "/sys/bus/usb/devices"
#define NXLINK_CLASS 0xff
#define NXLINK_SUBCLASS 0x4e
#define WRITE_TIMEOUT_MS 1000

// Reads one small named sysfs attribute. Only the specific files named by the
// callers are ever opened: some sysfs nodes on these boards have side effects
// on read (port role switches), so nothing here sweeps directories.
static int read_attr(const char* dir, const char* name, char* buf, int size) {
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	int r = (int)read(fd, buf, (size_t)size - 1);
	close(fd);
	if (r <= 0)
		return -1;
	buf[r] = 0;
	return r;
}

static long read_num(const char* dir, const char* name, int base) {
	char b[32];
	if (read_attr(dir, name, b, sizeof(b)) < 0)
		return -1;
	char* end;
	errno = 0;
	long v = strtol(b, &end, base);
	return (end == b || errno) ? -1 : v;
}

// Endpoint addresses come from the interface's ep_XX subdirectory names
// (directory names only, nothing inside is read).
static void find_endpoints(const char* ifdir, unsigned* ep_out, unsigned* ep_in) {
	*ep_out = *ep_in = 0;
	DIR* d = opendir(ifdir);
	if (!d)
		return;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "ep_", 3) != 0)
			continue;
		unsigned a = (unsigned)strtoul(e->d_name + 3, NULL, 16);
		if (a & 0x80)
			*ep_in = a;
		else
			*ep_out = a;
	}
	closedir(d);
}

static int try_claim(const char* name, UsbLinkHost* h, unsigned subclass) {
	char ifdir[512];
	snprintf(ifdir, sizeof(ifdir), USB_DEVICES "/%s", name);
	if (read_num(ifdir, "bInterfaceClass", 16) != NXLINK_CLASS || read_num(ifdir, "bInterfaceSubClass", 16) != (long)subclass)
		return -1;
	long ifnum = read_num(ifdir, "bInterfaceNumber", 16);
	// The interface entry is a symlink into the device's directory, so ".."
	// is the USB device that owns it.
	char devdir[520];
	snprintf(devdir, sizeof(devdir), "%s/..", ifdir);
	long bus = read_num(devdir, "busnum", 10);
	long dev = read_num(devdir, "devnum", 10);
	unsigned ep_out, ep_in;
	find_endpoints(ifdir, &ep_out, &ep_in);
	if (ifnum < 0 || bus <= 0 || dev <= 0 || !ep_out || !ep_in)
		return -1;

	char node[64];
	snprintf(node, sizeof(node), "/dev/bus/usb/%03ld/%03ld", bus, dev);
	int fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "usblink: open %s: %s\n", node, strerror(errno));
		return -1;
	}
	unsigned int n = (unsigned int)ifnum;
	if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &n) != 0) {
		fprintf(stderr, "usblink: claim %s if%ld: %s\n", node, ifnum, strerror(errno));
		close(fd);
		return -1;
	}
	h->fd = fd;
	h->ifnum = (int)ifnum;
	h->ep_out = ep_out;
	h->ep_in = ep_in;
	fprintf(stderr, "usblink: host %s if%d out=%02x in=%02x\n", node, h->ifnum, ep_out, ep_in);
	return 0;
}

int usblink_host_find_and_claim_subclass(UsbLinkHost* h, unsigned subclass) {
	h->fd = -1;
	DIR* d = opendir(USB_DEVICES);
	if (!d)
		return -1;
	int rc = -1;
	struct dirent* e;
	while ((e = readdir(d))) {
		// Interfaces are named "<bus>-<port>:<config>.<iface>"; plain device
		// and root-hub entries are skipped without reading anything.
		if (!strchr(e->d_name, ':'))
			continue;
		if (try_claim(e->d_name, h, subclass) == 0) {
			rc = 0;
			break;
		}
	}
	closedir(d);
	return rc;
}

int usblink_host_find_and_claim(UsbLinkHost* h) {
	return usblink_host_find_and_claim_subclass(h, NXLINK_SUBCLASS);
}

// One frame per transfer; usblink_frame pads so every frame ends with a short
// packet, which is what keeps transfers from merging.
int usblink_host_write_timeout(UsbLinkHost* h, const uint8_t* buf, int len, unsigned timeout_ms) {
	struct usbdevfs_bulktransfer bt = {
		.ep = h->ep_out,
		.len = (unsigned)len,
		.timeout = timeout_ms,
		.data = (void*)buf,
	};
	return ioctl(h->fd, USBDEVFS_BULK, &bt);
}

int usblink_host_write(UsbLinkHost* h, const uint8_t* buf, int len) {
	return usblink_host_write_timeout(h, buf, len, WRITE_TIMEOUT_MS);
}

int usblink_host_read(UsbLinkHost* h, uint8_t* buf, int len, unsigned timeout_ms) {
	struct usbdevfs_bulktransfer bt = {
		.ep = h->ep_in,
		.len = (unsigned)len,
		.timeout = timeout_ms,
		.data = buf,
	};
	int r = ioctl(h->fd, USBDEVFS_BULK, &bt);
	if (r < 0 && errno == ETIMEDOUT)
		return 0;
	return r;
}

void usblink_host_release(UsbLinkHost* h) {
	if (h->fd < 0)
		return;
	unsigned int n = (unsigned int)h->ifnum;
	ioctl(h->fd, USBDEVFS_RELEASEINTERFACE, &n);
	close(h->fd);
	h->fd = -1;
}
