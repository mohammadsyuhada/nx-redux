#ifndef USBLINK_STATE_H
#define USBLINK_STATE_H

#include <stdbool.h>

#include "usblink_link.h"

// Shell-sourceable status file the daemon publishes for the UI/scripts:
// USBLINK_LINK=up|down|error, USBLINK_SIDE=host|device|"", USBLINK_LOCAL_IP,
// USBLINK_PEER_IP (only when up), USBLINK_ERROR.
typedef struct {
	char link[8];
	char side[8];
	char local_ip[16];
	char peer_ip[16];
	char error[32];
} UsbLinkState;

int usblink_state_write(const char* path, const UsbLinkState* s);					  // 0 / -1, tmp file + rename
int usblink_state_read(const char* path, UsbLinkState* s);							  // 0 / -1; zeroes *s first
void usblink_state_from_link(const UsbLink* l, const char* error, UsbLinkState* out); // fills link/side/ips

#endif
