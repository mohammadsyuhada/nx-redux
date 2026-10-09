#ifndef SIOLINK_PROTO_H
#define SIOLINK_PROTO_H

#include <stdint.h>

// siolink: the gpSP lockstep link-cable channel in USB Cable sessions. Each
// message is [type][payload] (siolink_msg_*), one local datagram to/from
// usblink.elf, which carries it to the peer's usblink.elf as a ULF_SIO frame
// on the nxlink endpoints (usblink_sio.h). The USB-host side sends HELLO every
// SIOLINK_HELLO_MS until the gadget side answers HELLO_ACK, then sends CONFIRM;
// the host is up once its daemon accepted the CONFIRM, the gadget once it reads
// it. DATA frames are delivered in any phase, so a lost CONFIRM only costs one
// direction its fast path.
#define SIOLINK_PROTO_VERSION 1
#define SIOLINK_HELLO_MS 100
#define SIOLINK_HANDSHAKE_MS 10000
#define SIOLINK_PAYLOAD_MAX 256
#define SIOLINK_MSG_MAX (1 + SIOLINK_PAYLOAD_MAX)

typedef enum { SIOLINK_SIDE_HOST = 1,
			   SIOLINK_SIDE_DEVICE = 2 } SioLinkSide;

typedef struct {
	SioLinkSide side;
	int up, failed, hello_sent, peer_version;
	uint32_t start_ms, last_hello_ms;
} SioLinkHs;

// Action bits returned by the handshake calls.
enum {
	SIOHS_SEND_HELLO = 1,
	SIOHS_SEND_ACK = 2,
	SIOHS_SEND_CONFIRM = 4,
	SIOHS_UP = 8,
	SIOHS_FAIL = 16,
	SIOHS_DELIVER = 32,
};

void siolink_hs_init(SioLinkHs* h, SioLinkSide side, uint32_t now_ms);
// Host side only: HELLO cadence and the handshake timeout. The device side never fails.
int siolink_hs_tick(SioLinkHs* h, uint32_t now_ms);
int siolink_hs_on_frame(SioLinkHs* h, uint8_t type, const uint8_t* payload, int len);
// Host side: call once the local daemon accepted the CONFIRM. 0 unless a
// HELLO_ACK of our version was seen.
int siolink_hs_confirm_sent(SioLinkHs* h);
// "host" / "device" (UsbLinkState.side) -> SioLinkSide, else 0.
int siolink_side_from_state(const char* side);
// [type][payload] message codec. encode: bytes written, -1 (bad type/len, out
// too small). decode: payload length, -1 (bad type/size). Types are ULF_DATA,
// ULF_HELLO, ULF_HELLO_ACK, ULF_CONFIRM: never 0 (usblink's REGISTER byte) or ULF_SIO.
int siolink_msg_encode(uint8_t type, const void* payload, int len, uint8_t* out, int out_size);
int siolink_msg_decode(const uint8_t* buf, int n, uint8_t* type_out, const uint8_t** payload_out);

#endif
