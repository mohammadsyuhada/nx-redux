#define _GNU_SOURCE
#include "usblink_gadget.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define G "/sys/kernel/config/usb_gadget/g1"
#define G_UDC G "/UDC"
#define G_FUNC G "/functions/ffs.net"
#define G_LINK G "/configs/c.1/ffs.net"
#define FFS "/dev/usb-ffs/net"
#define UDC_SAVE "/tmp/usblink.udc"
#define UDC_CLASS "/sys/class/udc"

#define NXLINK_CLASS 0xff
#define NXLINK_SUBCLASS 0x4e
#define NXLINK_PROTOCOL 1

// FunctionFS v2 descriptors: one interface, bulk OUT ep1 + bulk IN ep2, for
// full and high speed. The gpSP lockstep link rides this same pair as ULF_SIO
// frames: tg5040's sunxi UDC refuses a gadget with a second bulk pair
// (-524 on bind, adbd warns), so there is no second interface. Integers are
// plain literals: both targets are little-endian and htole*() is not a
// constant expression.
static const struct {
	struct usb_functionfs_descs_head_v2 h;
	__le32 fs_count, hs_count;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio sink, source;
	} __attribute__((packed)) fs, hs;
} __attribute__((packed)) descs = {
	.h = {.magic = FUNCTIONFS_DESCRIPTORS_MAGIC_V2, .flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC, .length = sizeof(descs)},
	.fs_count = 3,
	.hs_count = 3,
#define INTF {.bLength = sizeof(struct usb_interface_descriptor), .bDescriptorType = USB_DT_INTERFACE, .bNumEndpoints = 2, .bInterfaceClass = NXLINK_CLASS, .bInterfaceSubClass = NXLINK_SUBCLASS, .bInterfaceProtocol = NXLINK_PROTOCOL, .iInterface = 1}
#define EP(addr, mps) {.bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT, .bEndpointAddress = addr, .bmAttributes = USB_ENDPOINT_XFER_BULK, .wMaxPacketSize = mps}
	.fs = {INTF, EP(1 | USB_DIR_OUT, 64), EP(2 | USB_DIR_IN, 64)},
	.hs = {INTF, EP(1 | USB_DIR_OUT, 512), EP(2 | USB_DIR_IN, 512)},
#undef INTF
#undef EP
};
_Static_assert(sizeof(descs) == 66, "FunctionFS descriptor layout: one interface, two bulk endpoints");

static const struct {
	struct usb_functionfs_strings_head h;
	struct {
		__le16 code;
		char s[sizeof "nxlink"];
	} __attribute__((packed)) lang0;
} __attribute__((packed)) strs = {
	.h = {.magic = FUNCTIONFS_STRINGS_MAGIC, .length = sizeof(strs), .str_count = 1, .lang_count = 1},
	.lang0 = {0x0409, "nxlink"},
};

// State shared with the signal path. detach() only touches these with
// async-signal-safe calls; `attached` makes a second detach (signal racing
// normal exit) a no-op.
static volatile sig_atomic_t attached;
static volatile sig_atomic_t linked; // ffs.net symlinked into c.1
static char udc_name[64];
static int udc_len;
static int ep0_fd = -1, ep1_fd = -1, ep2_fd = -1;

// Async-signal-safe write of a whole buffer to a sysfs/configfs file.
static int write_file(const char* path, const char* s, int len) {
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	int r = (int)write(fd, s, (size_t)len);
	close(fd);
	return r == len ? 0 : -1;
}

// Reads a one-line file into buf, newline stripped. Returns the length or -1.
static int read_line(const char* path, char* buf, int size) {
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	int r = (int)read(fd, buf, (size_t)size - 1);
	close(fd);
	if (r < 0)
		return -1;
	buf[r] = 0;
	while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == '\r' || buf[r - 1] == ' '))
		buf[--r] = 0;
	return r;
}

static void close_fd(int* fd) {
	if (*fd >= 0)
		close(*fd);
	*fd = -1;
}

// Adding a function to a bound gadget needs an unbind/rebind; adb on the same
// gadget drops for a moment and reconnects. On tg5040 (4.9) the unbind write
// itself reports ENODEV, yet the sequence works, so its result is ignored.
static void relink(const char* name, int len, int add) {
	write_file(G_UDC, "\n", 1);
	if (add)
		(void)symlink(G_FUNC, G_LINK);
	else
		unlink(G_LINK);
	if (len > 0)
		write_file(G_UDC, name, len);
}

int usblink_gadget_attach(int* out_fd, int* in_fd) {
	if (attached) {
		*out_fd = ep1_fd;
		*in_fd = ep2_fd;
		return 0;
	}
	// Piggyback on whatever UDC the stock gadget (adb) is bound to; an unbound
	// gadget means the port is not in device mode and there is nothing to join.
	udc_len = read_line(G_UDC, udc_name, sizeof(udc_name));
	if (udc_len <= 0) {
		udc_len = 0;
		fprintf(stderr, "usblink: gadget not bound to a UDC\n");
		return -1;
	}
	// Saved on disk too, so `stop` can rebind after this process was SIGKILLed.
	int sfd = open(UDC_SAVE, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (sfd >= 0) {
		if (write(sfd, udc_name, (size_t)udc_len) != udc_len)
			fprintf(stderr, "usblink: write %s: %s\n", UDC_SAVE, strerror(errno));
		close(sfd);
	}

	if (mkdir(G_FUNC, 0755) != 0 && errno != EEXIST) {
		fprintf(stderr, "usblink: mkdir %s: %s\n", G_FUNC, strerror(errno));
		goto fail;
	}
	mkdir("/dev/usb-ffs", 0755);
	mkdir(FFS, 0755);
	if (mount("net", FFS, "functionfs", 0, NULL) != 0 && errno != EBUSY) {
		fprintf(stderr, "usblink: mount functionfs: %s\n", strerror(errno));
		goto fail;
	}
	attached = 1; // from here on detach() has something to undo

	ep0_fd = open(FFS "/ep0", O_RDWR | O_CLOEXEC);
	if (ep0_fd < 0 || write(ep0_fd, &descs, sizeof(descs)) != (ssize_t)sizeof(descs) || write(ep0_fd, &strs, sizeof(strs)) != (ssize_t)sizeof(strs)) {
		fprintf(stderr, "usblink: ep0 descriptors: %s\n", strerror(errno));
		goto fail;
	}
	// ep1/ep2 only exist once the descriptors have been accepted.
	ep1_fd = open(FFS "/ep1", O_RDWR | O_CLOEXEC);
	ep2_fd = open(FFS "/ep2", O_RDWR | O_CLOEXEC);
	if (ep1_fd < 0 || ep2_fd < 0) {
		fprintf(stderr, "usblink: open endpoints: %s\n", strerror(errno));
		goto fail;
	}

	linked = 1; // set before the symlink so a signal mid-relink still unlinks
	write_file(G_UDC, "\n", 1);
	if (symlink(G_FUNC, G_LINK) != 0 && errno != EEXIST) {
		fprintf(stderr, "usblink: symlink %s: %s\n", G_LINK, strerror(errno));
		write_file(G_UDC, udc_name, udc_len);
		linked = 0;
		goto fail;
	}
	write_file(G_UDC, udc_name, udc_len);
	// A UDC that refuses the rebind (as tg5040's did for a second endpoint
	// pair) leaves UDC empty, and adb with it: take our function back out
	// (fail -> detach unlinks it and rebinds the stock gadget).
	char bound[64];
	if (read_line(G_UDC, bound, sizeof(bound)) <= 0) {
		fprintf(stderr, "usblink: gadget did not bind with ffs.net, backing out\n");
		goto fail;
	}

	*out_fd = ep1_fd;
	*in_fd = ep2_fd;
	return 0;

fail:
	attached = 1;
	usblink_gadget_detach();
	// The save file must only exist while a link may be live, so a later
	// repair never trusts a name from a failed attempt.
	unlink(UDC_SAVE);
	return -1;
}

int usblink_gadget_ep0_fd(void) {
	return ep0_fd;
}

// Called from SIGTERM/SIGINT handlers as well as normal exit. The function
// must be out of c.1 before ep0 closes, otherwise the gadget cannot rebind and
// adb stays dead until reboot. Only open/write/close/unlink/umount2/rmdir.
void usblink_gadget_detach(void) {
	if (!attached)
		return;
	attached = 0;
	if (linked) {
		linked = 0;
		relink(udc_name, udc_len, 0);
	}
	close_fd(&ep1_fd);
	close_fd(&ep2_fd);
	close_fd(&ep0_fd);
	umount2(FFS, MNT_DETACH);
	rmdir(G_FUNC);
}

// First UDC under /sys/class/udc, for when the saved name is missing. Both
// targets have exactly one UDC.
static int first_udc(char* buf, int size) {
	DIR* d = opendir(UDC_CLASS);
	if (!d)
		return -1;
	int len = -1;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		len = snprintf(buf, (size_t)size, "%s", e->d_name);
		if (len >= size)
			len = -1;
		break;
	}
	closedir(d);
	return len;
}

// Cleans up after a daemon that died without detaching (SIGKILL, crash): its
// ep0 is gone, so ffs.net is a dead function that keeps the gadget from
// binding. Safe to run when nothing is left over.
void usblink_gadget_repair(void) {
	char name[64];
	int saved = read_line(UDC_SAVE, name, sizeof(name));

	struct stat st;
	if (lstat(G_LINK, &st) == 0) {
		int len = saved > 0 ? saved : first_udc(name, sizeof(name));
		relink(name, len, 0);
	} else if (saved > 0) {
		// Killed between unbind and rebind: the link is gone but the gadget is
		// still unbound, so adb is down. Only done with a save file, which
		// proves our attach did the unbind; a gadget the firmware left
		// unbound (or the USB-host side) is never touched.
		char cur[64];
		if (read_line(G_UDC, cur, sizeof(cur)) == 0)
			write_file(G_UDC, name, saved);
	}
	umount2(FFS, MNT_DETACH);
	rmdir(G_FUNC);
	unlink(UDC_SAVE);
}
