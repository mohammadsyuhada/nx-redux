#include "usblink_sio.h"

#include <stddef.h>

int usblink_sio_local_kind(const uint8_t* buf, int n) {
	if (!buf || n < 1 || n > USBLINK_SIO_MAX)
		return USBLINK_SIO_LOCAL_DROP;
	if (buf[0] == USBLINK_SIO_REGISTER)
		return n == 1 ? USBLINK_SIO_LOCAL_REGISTER : USBLINK_SIO_LOCAL_DROP;
	return USBLINK_SIO_LOCAL_FORWARD;
}

UsbLinkSide usblink_sio_route(const UsbLink* l) {
	return l->phase == USBLINK_UP ? l->side : USBLINK_SIDE_NONE;
}
