#include "siolink_proto.h"

#include <string.h>

#include "usblink_frame.h" // ULF_* constants only

void siolink_hs_init(SioLinkHs* h, SioLinkSide side, uint32_t now_ms) {
	memset(h, 0, sizeof(*h));
	h->side = side;
	h->start_ms = now_ms;
}

int siolink_hs_tick(SioLinkHs* h, uint32_t now_ms) {
	if (h->side != SIOLINK_SIDE_HOST || h->up || h->failed)
		return 0;
	if (now_ms - h->start_ms >= SIOLINK_HANDSHAKE_MS) {
		h->failed = 1;
		return SIOHS_FAIL;
	}
	if (!h->hello_sent || now_ms - h->last_hello_ms >= SIOLINK_HELLO_MS) {
		h->hello_sent = 1;
		h->last_hello_ms = now_ms;
		return SIOHS_SEND_HELLO;
	}
	return 0;
}

int siolink_hs_on_frame(SioLinkHs* h, uint8_t type, const uint8_t* payload, int len) {
	if (type == ULF_DATA)
		return (len > 0 && len <= SIOLINK_PAYLOAD_MAX) ? SIOHS_DELIVER : 0;
	if (len < 1 || !payload)
		return 0;
	int version = payload[0];
	if (h->side == SIOLINK_SIDE_DEVICE) {
		// The host judges the version; the device always answers HELLO.
		if (type == ULF_HELLO)
			return SIOHS_SEND_ACK;
		if (type == ULF_CONFIRM && version == SIOLINK_PROTO_VERSION && !h->up) {
			h->up = 1;
			return SIOHS_UP;
		}
		return 0;
	}
	if (h->side == SIOLINK_SIDE_HOST && type == ULF_HELLO_ACK && !h->up && !h->failed) {
		if (version != SIOLINK_PROTO_VERSION) {
			h->failed = 1;
			h->peer_version = version;
			return SIOHS_FAIL;
		}
		h->peer_version = version;
		return SIOHS_SEND_CONFIRM;
	}
	return 0;
}

int siolink_hs_confirm_sent(SioLinkHs* h) {
	// Only a CONFIRM that answered a HELLO_ACK of our version brings the host up.
	if (h->side != SIOLINK_SIDE_HOST || h->up || h->failed || h->peer_version != SIOLINK_PROTO_VERSION)
		return 0;
	h->up = 1;
	return SIOHS_UP;
}

int siolink_side_from_state(const char* side) {
	if (!side)
		return 0;
	if (strcmp(side, "host") == 0)
		return SIOLINK_SIDE_HOST;
	if (strcmp(side, "device") == 0)
		return SIOLINK_SIDE_DEVICE;
	return 0;
}

// Types a siolink message may carry; 0 (usblink's REGISTER byte) and ULF_SIO never.
static int msg_type_ok(uint8_t t) {
	return t == ULF_DATA || t == ULF_HELLO || t == ULF_HELLO_ACK || t == ULF_CONFIRM;
}

int siolink_msg_encode(uint8_t type, const void* payload, int len, uint8_t* out, int out_size) {
	if (!msg_type_ok(type) || len < 0 || len > SIOLINK_PAYLOAD_MAX || 1 + len > out_size)
		return -1;
	out[0] = type;
	if (len)
		memcpy(out + 1, payload, (size_t)len);
	return 1 + len;
}

int siolink_msg_decode(const uint8_t* buf, int n, uint8_t* type_out, const uint8_t** payload_out) {
	if (!buf || n < 1 || n > SIOLINK_MSG_MAX || !msg_type_ok(buf[0]))
		return -1;
	*type_out = buf[0];
	*payload_out = buf + 1;
	return n - 1;
}
