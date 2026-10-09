#ifndef USBLINK_FRAME_H
#define USBLINK_FRAME_H

#include <stdint.h>

// One frame per USB bulk transfer: [len lo][len hi][type][payload...][pad?].
// A transfer whose length is a multiple of the endpoint's max packet size ends
// without a short packet, and the receiver's read() would then merge it with the
// next one; one pad byte whenever the framed size is a multiple of 64 (which
// covers 512 too) guarantees every frame ends short, on full and high speed.
#define USBLINK_FRAME_MAX 2048
#define USBLINK_FRAME_HDR 3
#define USBLINK_PAYLOAD_MAX (USBLINK_FRAME_MAX - USBLINK_FRAME_HDR - 1)
#define USBLINK_SIO_MAX 512 // largest ULF_SIO payload; a siolink message is at most 257 bytes

enum {
	ULF_DATA = 1,	   // one IP packet
	ULF_HELLO = 2,	   // USB-host side -> gadget side, payload: proto version
	ULF_HELLO_ACK = 3, // gadget side -> USB-host side, payload: proto version
	ULF_CONFIRM = 4,   // siolink only: never an outer frame, it is a siolink message type carried inside ULF_SIO
	ULF_SIO = 5,	   // one siolink message (netplay/siolink_proto.h) for/from the local minarch; linked port only
};

int usblink_frame_encode(uint8_t type, const uint8_t* payload, int len, uint8_t* out, int out_size);
int usblink_frame_decode(const uint8_t* buf, int n, uint8_t* type_out, const uint8_t** payload_out);

#endif
