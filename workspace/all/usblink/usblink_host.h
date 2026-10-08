#ifndef USBLINK_HOST_H
#define USBLINK_HOST_H

#include <stdint.h>

// USB-host side of the cable: finds the peer's "nxlink" FunctionFS interface
// (class 0xff, subclass 0x4e) in sysfs and drives its bulk endpoints via usbfs.
typedef struct {
	int fd;
	int ifnum;
	unsigned ep_out, ep_in;
} UsbLinkHost;

int usblink_host_find_and_claim(UsbLinkHost* h);								   // 0 claimed, -1 nothing found
int usblink_host_write(UsbLinkHost* h, const uint8_t* buf, int len);			   // bytes or -1 (errno set)
int usblink_host_read(UsbLinkHost* h, uint8_t* buf, int len, unsigned timeout_ms); // bytes, 0 on timeout, -1 error (errno)
void usblink_host_release(UsbLinkHost* h);

#endif
