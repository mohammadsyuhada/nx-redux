// Host-compiled unit tests for the pure usblink modules (no device toolchain).
// Build & run: workspace/all/usblink/tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../usblink_frame.h"

static void test_frame_roundtrip_sizes(void) {
	uint8_t payload[USBLINK_PAYLOAD_MAX], out[USBLINK_FRAME_MAX];
	for (int i = 0; i < (int)sizeof(payload); i++)
		payload[i] = (uint8_t)(i * 7);
	// Every length up to MTU+, including the ones whose framed size lands on a
	// 64-byte (full-speed) or 512-byte (high-speed) packet boundary.
	for (int len = 0; len <= 1600; len++) {
		int n = usblink_frame_encode(ULF_DATA, payload, len, out, sizeof(out));
		assert(n >= len + USBLINK_FRAME_HDR);
		assert(n % 64 != 0); // always ends in a short packet
		uint8_t type = 0;
		const uint8_t* p = NULL;
		assert(usblink_frame_decode(out, n, &type, &p) == len);
		assert(type == ULF_DATA);
		assert(memcmp(p, payload, len) == 0);
	}
}

static void test_frame_padding_boundaries(void) {
	uint8_t payload[1024] = {0}, out[USBLINK_FRAME_MAX];
	// 61 + 3 = 64 -> padded to 65; 509 + 3 = 512 -> 513; 62 + 3 = 65 -> unpadded.
	assert(usblink_frame_encode(ULF_DATA, payload, 61, out, sizeof(out)) == 65);
	assert(usblink_frame_encode(ULF_DATA, payload, 509, out, sizeof(out)) == 513);
	assert(usblink_frame_encode(ULF_DATA, payload, 62, out, sizeof(out)) == 65);
}

static void test_frame_rejects(void) {
	uint8_t buf[USBLINK_FRAME_MAX] = {0}, out[USBLINK_FRAME_MAX];
	uint8_t type;
	const uint8_t* p;
	assert(usblink_frame_encode(ULF_DATA, buf, USBLINK_PAYLOAD_MAX + 1, out, sizeof(out)) == -1);
	assert(usblink_frame_encode(ULF_DATA, buf, -1, out, sizeof(out)) == -1);
	assert(usblink_frame_encode(ULF_DATA, buf, 100, out, 50) == -1);
	assert(usblink_frame_decode(buf, 2, &type, &p) == -1); // shorter than header
	buf[0] = 10;
	buf[1] = 0;
	buf[2] = ULF_DATA; // claims 10, carries 5
	assert(usblink_frame_decode(buf, 8, &type, &p) == -1);
	buf[0] = 1;
	buf[1] = 0;											   // claims 1, 3 trailing bytes
	assert(usblink_frame_decode(buf, 7, &type, &p) == -1); // more than one pad byte
	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0; // unknown type 0
	assert(usblink_frame_decode(buf, 3, &type, &p) == -1);
}

static void test_frame_control(void) {
	uint8_t v = 1, out[16];
	int n = usblink_frame_encode(ULF_HELLO, &v, 1, out, sizeof(out));
	assert(n == 4);
	uint8_t type;
	const uint8_t* p;
	assert(usblink_frame_decode(out, n, &type, &p) == 1 && type == ULF_HELLO && p[0] == 1);
}

int main(void) {
	test_frame_roundtrip_sizes();
	test_frame_padding_boundaries();
	test_frame_rejects();
	test_frame_control();
	printf("usblink tests: OK\n");
	return 0;
}
