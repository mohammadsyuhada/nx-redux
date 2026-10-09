#ifndef USBLINK_SIO_H
#define USBLINK_SIO_H

#include <stdint.h>

#include "usblink_frame.h"
#include "usblink_link.h"

// Local end of the gpSP lockstep link channel ("siolink"). The daemon owns a
// Unix datagram socket at USBLINK_SIO_SOCK. The minarch on this handheld binds
// its own (USBLINK_SIO_CLIENT_FMT), connects to the daemon's and sends the
// 1-byte USBLINK_SIO_REGISTER (echoed back as the ack). From then on each
// datagram either way is one ULF_SIO payload: the daemon sends it out on the
// linked USB port, or hands the peer's to the registered client. The daemon is
// a pipe and never looks inside a payload (netplay/siolink_proto.h does).
#define USBLINK_SIO_SOCK "/tmp/usblink.sio"
#define USBLINK_SIO_CLIENT_FMT "/tmp/usblink.sio.%d" // a client's own socket, by pid
#define USBLINK_SIO_REGISTER 0x00					 // 1-byte datagram client -> daemon; the daemon echoes it back as the ack

enum { USBLINK_SIO_LOCAL_DROP = 0,
	   USBLINK_SIO_LOCAL_REGISTER = 1,
	   USBLINK_SIO_LOCAL_FORWARD = 2 };

int usblink_sio_local_kind(const uint8_t* buf, int n); // what a datagram on USBLINK_SIO_SOCK is
UsbLinkSide usblink_sio_route(const UsbLink* l);	   // port to send SIO on; USBLINK_SIDE_NONE = drop (link down)

#endif
