#include "usblink_frame.h"

#include <string.h>

int usblink_frame_encode(uint8_t type, const uint8_t* payload, int len, uint8_t* out, int out_size) {
	if (len < 0 || len > USBLINK_PAYLOAD_MAX)
		return -1;
	int total = len + USBLINK_FRAME_HDR;
	int padded = (total % 64 == 0) ? total + 1 : total;
	if (padded > out_size)
		return -1;
	out[0] = (uint8_t)(len & 0xff);
	out[1] = (uint8_t)(len >> 8);
	out[2] = type;
	if (len)
		memcpy(out + USBLINK_FRAME_HDR, payload, (size_t)len);
	if (padded != total)
		out[total] = 0;
	return padded;
}

int usblink_frame_decode(const uint8_t* buf, int n, uint8_t* type_out, const uint8_t** payload_out) {
	if (n < USBLINK_FRAME_HDR)
		return -1;
	int len = buf[0] | (buf[1] << 8);
	int extra = n - USBLINK_FRAME_HDR - len;
	if (len > USBLINK_PAYLOAD_MAX || extra < 0 || extra > 1)
		return -1;
	if (buf[2] < ULF_DATA || buf[2] > ULF_SIO)
		return -1;
	*type_out = buf[2];
	*payload_out = buf + USBLINK_FRAME_HDR;
	return len;
}
