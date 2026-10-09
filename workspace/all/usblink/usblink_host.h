#ifndef USBLINK_HOST_H
#define USBLINK_HOST_H

#include <stdint.h>

// USB-host side of the cable: finds a peer interface (class 0xff, subclass
// 0x4e "nxlink" or 0x4f "nxsio") in sysfs and drives its bulk endpoints via
// usbfs.
typedef struct {
	int fd;
	int ifnum;
	unsigned ep_out, ep_in;
} UsbLinkHost;

int usblink_host_find_and_claim_subclass(UsbLinkHost* h, unsigned subclass);					  // 0 claimed, -1 none
int usblink_host_find_and_claim(UsbLinkHost* h);												  // 0 claimed, -1 nothing found
int usblink_host_write_timeout(UsbLinkHost* h, const uint8_t* buf, int len, unsigned timeout_ms); // bytes or -1 (errno set)
int usblink_host_write(UsbLinkHost* h, const uint8_t* buf, int len);							  // bytes or -1 (errno set)
int usblink_host_read(UsbLinkHost* h, uint8_t* buf, int len, unsigned timeout_ms);				  // bytes, 0 on timeout, -1 error (errno)
void usblink_host_release(UsbLinkHost* h);

#endif
