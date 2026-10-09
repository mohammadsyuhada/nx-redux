// Host-compiled unit tests for the pure usblink modules (no device toolchain).
// Build & run: workspace/all/usblink/tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../usblink_frame.h"
#include "../usblink_link.h"
#include "../usblink_state.h"
#include "../usblink_sio.h"

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

static uint8_t V = USBLINK_PROTO_VERSION;

static void test_link_host_handshake_and_keepalive(void) {
	UsbLink l;
	usblink_link_init(&l);
	assert(usblink_link_tick(&l, 0, 1000) == 0); // nothing attached: silent
	assert(usblink_link_tick(&l, 1, 1000) & USBLINK_ACT_SEND_HELLO);
	assert(usblink_link_tick(&l, 1, 1100) == 0); // < 250 ms
	assert(usblink_link_tick(&l, 1, 1250) & USBLINK_ACT_SEND_HELLO);
	int a = usblink_link_on_frame(&l, USBLINK_SIDE_HOST, ULF_HELLO_ACK, &V, 1, 1300);
	assert(a & USBLINK_ACT_UP);
	assert(l.phase == USBLINK_UP && l.side == USBLINK_SIDE_HOST);
	assert(usblink_link_tick(&l, 1, 1500) == 0); // keepalive is 1 s once up
	assert(usblink_link_tick(&l, 1, 2250) & USBLINK_ACT_SEND_HELLO);
	usblink_link_on_frame(&l, USBLINK_SIDE_HOST, ULF_HELLO_ACK, &V, 1, 2260);
	assert(usblink_link_tick(&l, 1, 5000) & USBLINK_ACT_SEND_HELLO); // 2740 ms since rx: still up
	assert(l.phase == USBLINK_UP);
}

static void test_link_dead_after_silence_then_relink(void) {
	UsbLink l;
	usblink_link_init(&l);
	usblink_link_tick(&l, 1, 0);
	usblink_link_on_frame(&l, USBLINK_SIDE_HOST, ULF_HELLO_ACK, &V, 1, 10);
	assert(usblink_link_tick(&l, 1, 10 + USBLINK_DEAD_MS) & USBLINK_ACT_DOWN);
	assert(l.phase == USBLINK_IDLE && l.side == USBLINK_SIDE_NONE);
	// Re-plug: hellos resume at the idle cadence and a fresh ACK relinks.
	assert(usblink_link_tick(&l, 1, 10 + USBLINK_DEAD_MS + 250) & USBLINK_ACT_SEND_HELLO);
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_HOST, ULF_HELLO_ACK, &V, 1, 4000) & USBLINK_ACT_UP);
}

static void test_link_device_side(void) {
	UsbLink l;
	usblink_link_init(&l);
	int a = usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_HELLO, &V, 1, 100);
	assert((a & USBLINK_ACT_SEND_ACK) && (a & USBLINK_ACT_UP));
	assert(l.side == USBLINK_SIDE_DEVICE);
	// Device side never originates hellos, even with something on its host port.
	assert((usblink_link_tick(&l, 1, 2000) & USBLINK_ACT_SEND_HELLO) == 0);
	// Keepalive hellos refresh it; silence drops it.
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_HELLO, &V, 1, 1100) == USBLINK_ACT_SEND_ACK);
	assert(usblink_link_tick(&l, 0, 1100 + USBLINK_DEAD_MS) & USBLINK_ACT_DOWN);
}

static void test_link_data_only_on_linked_side(void) {
	UsbLink l;
	uint8_t pkt[20] = {0x45};
	usblink_link_init(&l);
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_HOST, ULF_DATA, pkt, 20, 5) == 0); // not up
	usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_HELLO, &V, 1, 10);
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_DATA, pkt, 20, 20) == USBLINK_ACT_DELIVER);
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_HOST, ULF_DATA, pkt, 20, 30) == 0); // other side ignored
}

static void test_link_two_cables_first_wins(void) {
	UsbLink l;
	usblink_link_init(&l);
	usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_HELLO, &V, 1, 10);
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_HOST, ULF_HELLO_ACK, &V, 1, 20) == 0);
	assert(l.side == USBLINK_SIDE_DEVICE);
	// A HELLO arriving while we are host-linked is still ACKed but changes nothing.
	UsbLink h;
	usblink_link_init(&h);
	usblink_link_on_frame(&h, USBLINK_SIDE_HOST, ULF_HELLO_ACK, &V, 1, 10);
	assert(usblink_link_on_frame(&h, USBLINK_SIDE_DEVICE, ULF_HELLO, &V, 1, 20) == USBLINK_ACT_SEND_ACK);
	assert(h.side == USBLINK_SIDE_HOST);
}

static void test_link_version_mismatch(void) {
	uint8_t bad = USBLINK_PROTO_VERSION + 1;
	UsbLink l;
	usblink_link_init(&l);
	int a = usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_HELLO, &bad, 1, 10);
	assert((a & USBLINK_ACT_SEND_ACK) && (a & USBLINK_ACT_ERROR) && !(a & USBLINK_ACT_UP));
	assert(l.phase == USBLINK_ERROR && l.peer_version == bad);
	assert(usblink_link_tick(&l, 1, 9999) == 0); // error is terminal
	UsbLink h;
	usblink_link_init(&h);
	assert(usblink_link_on_frame(&h, USBLINK_SIDE_HOST, ULF_HELLO_ACK, &bad, 1, 10) & USBLINK_ACT_ERROR);
	// Truncated control frames are ignored, not treated as version 0.
	UsbLink t;
	usblink_link_init(&t);
	assert(usblink_link_on_frame(&t, USBLINK_SIDE_DEVICE, ULF_HELLO, &V, 0, 10) == 0);
}

static void test_link_host_detach(void) {
	UsbLink l;
	usblink_link_init(&l);
	usblink_link_on_frame(&l, USBLINK_SIDE_HOST, ULF_HELLO_ACK, &V, 1, 10);
	assert(usblink_link_on_host_detached(&l) == USBLINK_ACT_DOWN);
	assert(usblink_link_on_host_detached(&l) == 0);
}

static void test_state_roundtrip(void) {
	char path[] = "/tmp/usblink_state_testXXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	close(fd);
	UsbLink l;
	usblink_link_init(&l);
	usblink_link_on_frame(&l, USBLINK_SIDE_HOST, ULF_HELLO_ACK, &V, 1, 10);
	UsbLinkState s, r;
	usblink_state_from_link(&l, "", &s);
	assert(usblink_state_write(path, &s) == 0);
	assert(usblink_state_read(path, &r) == 0);
	assert(!strcmp(r.link, "up") && !strcmp(r.side, "host"));
	assert(!strcmp(r.local_ip, "10.99.0.1") && !strcmp(r.peer_ip, "10.99.0.2"));
	usblink_link_init(&l);
	usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_HELLO, &V, 1, 10);
	usblink_state_from_link(&l, "", &s);
	assert(!strcmp(s.local_ip, "10.99.0.2") && !strcmp(s.peer_ip, "10.99.0.1"));
	usblink_link_init(&l);
	usblink_state_from_link(&l, "tun", &s);
	usblink_state_write(path, &s);
	usblink_state_read(path, &r);
	assert(!strcmp(r.link, "down") && r.side[0] == 0 && r.peer_ip[0] == 0 && !strcmp(r.error, "tun"));
	l.phase = USBLINK_ERROR;
	usblink_state_from_link(&l, "version", &s);
	assert(!strcmp(s.link, "error") && !strcmp(s.error, "version"));
	unlink(path);
	assert(usblink_state_read(path, &r) == -1);
}

static void test_frame_confirm_type(void) {
	uint8_t v = 1, out[16], type;
	const uint8_t* p;
	int n = usblink_frame_encode(ULF_CONFIRM, &v, 1, out, sizeof(out));
	assert(usblink_frame_decode(out, n, &type, &p) == 1 && type == ULF_CONFIRM);
	out[2] = ULF_SIO + 1;
	assert(usblink_frame_decode(out, n, &type, &p) == -1);
	// The IP link ignores it as an outer frame (it only travels inside ULF_SIO).
	UsbLink l;
	usblink_link_init(&l);
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_CONFIRM, &v, 1, 10) == 0);
}

static void test_frame_sio_type(void) {
	uint8_t pl[25] = {1, 'N', 'X', 'L', 'S'}, out[64], type;
	const uint8_t* q;
	int n = usblink_frame_encode(ULF_SIO, pl, 25, out, sizeof(out));
	assert(usblink_frame_decode(out, n, &type, &q) == 25 && type == ULF_SIO && memcmp(q, pl, 25) == 0);
	out[2] = ULF_SIO + 1;
	assert(usblink_frame_decode(out, n, &type, &q) == -1);
	// The largest SIO frame (pad included) fits one 512-byte high-speed packet.
	static uint8_t big[USBLINK_SIO_MAX], fb[1024];
	assert(USBLINK_SIO_MAX == 508);
	n = usblink_frame_encode(ULF_SIO, big, USBLINK_SIO_MAX, fb, sizeof(fb));
	assert(n > 0 && n <= 512);
}

static void test_link_sio_only_when_up_on_linked_side(void) {
	UsbLink l;
	uint8_t pl[USBLINK_SIO_MAX + 1];
	memset(pl, 1, sizeof(pl));
	usblink_link_init(&l);
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_SIO, pl, 25, 5) == 0); // not up: dropped
	assert(usblink_sio_route(&l) == USBLINK_SIDE_NONE);
	usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_HELLO, &V, 1, 10);
	assert(usblink_sio_route(&l) == USBLINK_SIDE_DEVICE);
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_SIO, pl, 25, 2000) == USBLINK_ACT_SIO);
	assert(l.last_rx_ms == 2000);													  // SIO traffic keeps the link alive like IP does
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_HOST, ULF_SIO, pl, 25, 2010) == 0); // other port
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_SIO, pl, 0, 2020) == 0);
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_SIO, pl, USBLINK_SIO_MAX + 1, 2030) == 0);
	assert(usblink_link_on_frame(&l, USBLINK_SIDE_DEVICE, ULF_SIO, pl, USBLINK_SIO_MAX, 2040) == USBLINK_ACT_SIO);
	assert(usblink_link_tick(&l, 0, 2040 + USBLINK_DEAD_MS) & USBLINK_ACT_DOWN);
	assert(usblink_sio_route(&l) == USBLINK_SIDE_NONE); // link down: the daemon drops client payloads
	UsbLink h;
	usblink_link_init(&h);
	usblink_link_on_frame(&h, USBLINK_SIDE_HOST, ULF_HELLO_ACK, &V, 1, 10);
	assert(usblink_sio_route(&h) == USBLINK_SIDE_HOST);
}

static void test_sio_local_kind(void) {
	uint8_t b[USBLINK_SIO_MAX + 1];
	memset(b, 1, sizeof(b));
	uint8_t reg = USBLINK_SIO_REGISTER;
	assert(usblink_sio_local_kind(&reg, 1) == USBLINK_SIO_LOCAL_REGISTER);
	assert(usblink_sio_local_kind(b, 1) == USBLINK_SIO_LOCAL_FORWARD);
	assert(usblink_sio_local_kind(b, USBLINK_SIO_MAX) == USBLINK_SIO_LOCAL_FORWARD);
	assert(usblink_sio_local_kind(b, USBLINK_SIO_MAX + 1) == USBLINK_SIO_LOCAL_DROP); // truncated by recvfrom
	assert(usblink_sio_local_kind(b, 0) == USBLINK_SIO_LOCAL_DROP);
	assert(usblink_sio_local_kind(NULL, 1) == USBLINK_SIO_LOCAL_DROP);
	b[0] = USBLINK_SIO_REGISTER;
	assert(usblink_sio_local_kind(b, 2) == USBLINK_SIO_LOCAL_DROP); // payloads never start with 0
}

int main(void) {
	test_frame_roundtrip_sizes();
	test_frame_padding_boundaries();
	test_frame_rejects();
	test_frame_control();
	test_frame_confirm_type();
	test_frame_sio_type();
	test_link_sio_only_when_up_on_linked_side();
	test_sio_local_kind();
	test_link_host_handshake_and_keepalive();
	test_link_dead_after_silence_then_relink();
	test_link_device_side();
	test_link_data_only_on_linked_side();
	test_link_two_cables_first_wins();
	test_link_version_mismatch();
	test_link_host_detach();
	test_state_roundtrip();
	printf("usblink tests: OK\n");
	return 0;
}
