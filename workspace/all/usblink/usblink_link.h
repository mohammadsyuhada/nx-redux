#ifndef USBLINK_LINK_H
#define USBLINK_LINK_H

#include <stdint.h>

// Handshake/keepalive state machine for one USB cable between two handhelds.
// The side that is USB host sends HELLOs; the gadget side answers HELLO_ACK.
// Pure logic: the caller does the I/O named by the returned USBLINK_ACT_* bits.
#define USBLINK_PROTO_VERSION 2	  // 2: ULF_SIO exists and the peer's gpSP knows "lockstep"
#define USBLINK_HELLO_IDLE_MS 250 // hello cadence while not linked
#define USBLINK_HELLO_UP_MS 1000  // keepalive cadence once linked
#define USBLINK_DEAD_MS 3000	  // no frame for this long = link down

typedef enum { USBLINK_SIDE_NONE = 0,
			   USBLINK_SIDE_HOST,
			   USBLINK_SIDE_DEVICE } UsbLinkSide;
typedef enum { USBLINK_IDLE = 0,
			   USBLINK_UP,
			   USBLINK_ERROR } UsbLinkPhase;

typedef struct {
	UsbLinkPhase phase;
	UsbLinkSide side; // which port the link runs over (valid while UP)
	uint32_t last_rx_ms;
	uint32_t last_hello_ms;
	int peer_version; // set on a version mismatch
	int hello_sent;	  // first hello goes out immediately, then on cadence
} UsbLink;

enum {
	USBLINK_ACT_SEND_HELLO = 1,
	USBLINK_ACT_SEND_ACK = 2,
	USBLINK_ACT_UP = 4,
	USBLINK_ACT_DOWN = 8,
	USBLINK_ACT_ERROR = 16,
	USBLINK_ACT_DELIVER = 32,
	USBLINK_ACT_SIO = 64, // hand the ULF_SIO payload to the registered siolink client
};

void usblink_link_init(UsbLink* l);
// via = the port the frame arrived on (HOST = our USB host port, DEVICE = our gadget).
int usblink_link_on_frame(UsbLink* l, UsbLinkSide via, uint8_t type, const uint8_t* payload, int len, uint32_t now_ms);
int usblink_link_tick(UsbLink* l, int host_attached, uint32_t now_ms);
int usblink_link_on_host_detached(UsbLink* l);

#endif
