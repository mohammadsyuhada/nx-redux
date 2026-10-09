// Host-compiled tests: siolink handshake rules and gpsp_serial resolution.
// Build & run: workspace/all/netplay/tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../gbalink_mode.h"
#include "../siolink_proto.h"
#include "usblink_frame.h"

static uint8_t V = SIOLINK_PROTO_VERSION;

static void test_hs_host_hello_cadence_and_fail(void) {
	SioLinkHs h;
	siolink_hs_init(&h, SIOLINK_SIDE_HOST, 1000);
	assert(siolink_hs_tick(&h, 1000) == SIOHS_SEND_HELLO);
	assert(siolink_hs_tick(&h, 1050) == 0);
	assert(siolink_hs_tick(&h, 1100) == SIOHS_SEND_HELLO);
	assert(siolink_hs_tick(&h, 1000 + SIOLINK_HANDSHAKE_MS) == SIOHS_FAIL);
	assert(h.failed && siolink_hs_tick(&h, 999999) == 0);
}

static void test_hs_full(void) {
	SioLinkHs host, dev;
	siolink_hs_init(&host, SIOLINK_SIDE_HOST, 0);
	siolink_hs_init(&dev, SIOLINK_SIDE_DEVICE, 0);
	assert(siolink_hs_on_frame(&dev, ULF_HELLO, &V, 1) == SIOHS_SEND_ACK && !dev.up);
	assert(siolink_hs_on_frame(&host, ULF_HELLO_ACK, &V, 1) == SIOHS_SEND_CONFIRM && !host.up);
	assert(siolink_hs_confirm_sent(&host) == SIOHS_UP && host.up);
	assert(siolink_hs_on_frame(&dev, ULF_CONFIRM, &V, 1) == SIOHS_UP && dev.up);
	assert(siolink_hs_on_frame(&dev, ULF_CONFIRM, &V, 1) == 0);	   // once
	assert(siolink_hs_tick(&host, 50) == 0);					   // no hellos once up
	assert(siolink_hs_on_frame(&host, ULF_HELLO_ACK, &V, 1) == 0); // late duplicate ACK
	assert(siolink_hs_confirm_sent(&host) == 0);
}

static void test_hs_version_and_malformed(void) {
	uint8_t bad = SIOLINK_PROTO_VERSION + 1;
	SioLinkHs host, dev;
	siolink_hs_init(&host, SIOLINK_SIDE_HOST, 0);
	siolink_hs_init(&dev, SIOLINK_SIDE_DEVICE, 0);
	assert(siolink_hs_on_frame(&host, ULF_HELLO_ACK, &bad, 1) == SIOHS_FAIL);
	assert(host.failed && host.peer_version == bad && siolink_hs_confirm_sent(&host) == 0);
	assert(siolink_hs_on_frame(&dev, ULF_HELLO, &bad, 1) == SIOHS_SEND_ACK);
	assert(siolink_hs_on_frame(&dev, ULF_CONFIRM, &bad, 1) == 0 && !dev.up);
	assert(siolink_hs_on_frame(&dev, ULF_HELLO, &V, 0) == 0);	  // truncated
	assert(siolink_hs_on_frame(&dev, ULF_HELLO_ACK, &V, 1) == 0); // wrong side
	SioLinkHs h2;
	siolink_hs_init(&h2, SIOLINK_SIDE_HOST, 0);
	assert(siolink_hs_on_frame(&h2, ULF_HELLO, &V, 1) == 0);
	assert(siolink_hs_on_frame(&h2, ULF_CONFIRM, &V, 1) == 0);
	assert(siolink_hs_on_frame(&h2, ULF_HELLO_ACK, &V, 0) == 0);
}

static void test_hs_data_always_delivered(void) {
	uint8_t p[SIOLINK_PAYLOAD_MAX + 1] = {'N', 'X', 'L', 'S'};
	SioLinkHs host, dev;
	siolink_hs_init(&host, SIOLINK_SIDE_HOST, 0);
	siolink_hs_init(&dev, SIOLINK_SIDE_DEVICE, 0);
	assert(siolink_hs_on_frame(&host, ULF_DATA, p, 24) == SIOHS_DELIVER); // not up yet: still ours
	assert(siolink_hs_on_frame(&dev, ULF_DATA, p, 24) == SIOHS_DELIVER);
	assert(siolink_hs_on_frame(&dev, ULF_DATA, p, SIOLINK_PAYLOAD_MAX + 1) == 0);
	assert(siolink_hs_on_frame(&dev, ULF_DATA, p, 0) == 0);
}

static void test_hs_device_never_fails(void) {
	SioLinkHs dev;
	siolink_hs_init(&dev, SIOLINK_SIDE_DEVICE, 0);
	assert(siolink_hs_tick(&dev, 600000) == 0 && !dev.failed);
	assert(siolink_hs_on_frame(&dev, ULF_CONFIRM, &V, 1) == SIOHS_UP);
}

static void test_side_from_state(void) {
	assert(siolink_side_from_state("host") == SIOLINK_SIDE_HOST);
	assert(siolink_side_from_state("device") == SIOLINK_SIDE_DEVICE);
	assert(siolink_side_from_state("") == 0 && siolink_side_from_state(NULL) == 0);
}

static void test_serial_option(void) {
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "auto", "usb"), "auto_cable"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "lockstep", "usb"), "lockstep"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "rfu", "usb"), "rfu"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "auto", "wifi"), "auto"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "lockstep", "hotspot"), "auto"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "lockstep", NULL), "auto")); // single player
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "mul_poke", NULL), "mul_poke"));
	const char* v = "auto";
	assert(gbalink_serial_option("gpsp_rtc", v, "usb") == v);
	assert(gbalink_serial_option("gpsp_serial", NULL, "usb") == NULL);
}

int main(void) {
	test_hs_host_hello_cadence_and_fail();
	test_hs_full();
	test_hs_version_and_malformed();
	test_hs_data_always_delivered();
	test_hs_device_never_fails();
	test_side_from_state();
	test_serial_option();
	printf("netplay link tests: OK\n");
	return 0;
}
