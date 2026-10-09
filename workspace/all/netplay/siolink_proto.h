#ifndef SIOLINK_PROTO_H
#define SIOLINK_PROTO_H

#include <stdint.h>

// siolink: the gpSP lockstep link-cable channel on the nxsio USB interface.
// Frames use the usblink_frame codec. The USB-host side sends HELLO every
// SIOLINK_HELLO_MS until the gadget side answers HELLO_ACK, then sends CONFIRM;
// the host is up once its CONFIRM write completes, the gadget once it reads it.
// DATA frames are delivered in any phase.
#define SIOLINK_PROTO_VERSION 1
#define SIOLINK_HELLO_MS 100
#define SIOLINK_HANDSHAKE_MS 10000
#define SIOLINK_PAYLOAD_MAX 256

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
// Host side: call once the CONFIRM write has completed.
int siolink_hs_confirm_sent(SioLinkHs* h);
// "host" / "device" (UsbLinkState.side) -> SioLinkSide, else 0.
int siolink_side_from_state(const char* side);

#endif
