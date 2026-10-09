#include "usblink_link.h"

#include <string.h>

#include "usblink_frame.h"

void usblink_link_init(UsbLink* l) {
	memset(l, 0, sizeof(*l));
	l->last_hello_ms = 0;
	l->last_rx_ms = 0;
}

int usblink_link_tick(UsbLink* l, int host_attached, uint32_t now_ms) {
	if (l->phase == USBLINK_ERROR)
		return 0;
	if (l->phase == USBLINK_UP && now_ms - l->last_rx_ms >= USBLINK_DEAD_MS) {
		l->phase = USBLINK_IDLE;
		l->side = USBLINK_SIDE_NONE;
		return USBLINK_ACT_DOWN;
	}
	int act = 0;
	if (host_attached && (l->phase != USBLINK_UP || l->side == USBLINK_SIDE_HOST)) {
		uint32_t cadence = (l->phase == USBLINK_UP) ? USBLINK_HELLO_UP_MS : USBLINK_HELLO_IDLE_MS;
		if (!l->hello_sent || now_ms - l->last_hello_ms >= cadence) {
			l->hello_sent = 1;
			l->last_hello_ms = now_ms;
			act |= USBLINK_ACT_SEND_HELLO;
		}
	}
	return act;
}

// HELLO (gadget side) and HELLO_ACK (host side) share everything but the ACK reply.
static int on_control(UsbLink* l, UsbLinkSide via, uint8_t version, uint32_t now_ms) {
	if (version != USBLINK_PROTO_VERSION) {
		l->phase = USBLINK_ERROR;
		l->peer_version = version;
		return USBLINK_ACT_ERROR;
	}
	if (l->phase == USBLINK_IDLE) {
		l->phase = USBLINK_UP;
		l->side = via;
		l->last_rx_ms = now_ms;
		return USBLINK_ACT_UP;
	}
	if (l->phase == USBLINK_UP && l->side == via)
		l->last_rx_ms = now_ms;
	return 0;
}

int usblink_link_on_frame(UsbLink* l, UsbLinkSide via, uint8_t type, const uint8_t* payload, int len, uint32_t now_ms) {
	if (via == USBLINK_SIDE_DEVICE && type == ULF_HELLO) {
		if (len < 1)
			return 0;
		// Always answered: the ACK carries OUR version so the host can diagnose.
		return USBLINK_ACT_SEND_ACK | on_control(l, via, payload[0], now_ms);
	}
	if (via == USBLINK_SIDE_HOST && type == ULF_HELLO_ACK) {
		if (len < 1)
			return 0;
		return on_control(l, via, payload[0], now_ms);
	}
	if (type == ULF_DATA) {
		if (l->phase == USBLINK_UP && via == l->side) {
			l->last_rx_ms = now_ms;
			return USBLINK_ACT_DELIVER;
		}
	}
	if (type == ULF_SIO) {
		// gpSP lockstep link traffic for the local minarch (usblink_sio.h).
		// Only on the linked port; it counts as traffic for the keepalive.
		if (l->phase == USBLINK_UP && via == l->side && len >= 1 && len <= USBLINK_SIO_MAX) {
			l->last_rx_ms = now_ms;
			return USBLINK_ACT_SIO;
		}
		return 0;
	}
	return 0;
}

int usblink_link_on_host_detached(UsbLink* l) {
	if (l->phase == USBLINK_UP && l->side == USBLINK_SIDE_HOST) {
		l->phase = USBLINK_IDLE;
		l->side = USBLINK_SIDE_NONE;
		return USBLINK_ACT_DOWN;
	}
	return 0;
}
