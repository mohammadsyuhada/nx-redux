// Host-compiled unit tests for sync_proto (no device toolchain).
// Build & run: workspace/all/sync/tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sync_proto.h"

static SyncPeer parse(const char* s) {
	SyncPeer p;
	sync_parse_payload(s, strlen(s), &p);
	return p;
}

static void test_parse_flags(void) {
	SyncPeer p = parse("192.168.1.5");
	assert(!strcmp(p.ip, "192.168.1.5") && !p.roms && !p.client_only && !p.filters);
	p = parse("192.168.1.5:0");
	assert(!strcmp(p.ip, "192.168.1.5") && !p.roms && !p.client_only && !p.filters);
	p = parse("192.168.1.5:R");
	assert(!strcmp(p.ip, "192.168.1.5") && p.roms && !p.client_only && !p.filters);
	p = parse("10.99.0.1:c");
	assert(!strcmp(p.ip, "10.99.0.1") && !p.roms && p.client_only && p.filters);
	p = parse("10.99.0.1:C");
	assert(!strcmp(p.ip, "10.99.0.1") && p.roms && p.client_only && p.filters);
	p = parse("192.168.1.5:x"); // unknown flag: ROMs off, as old firmware reads it
	assert(!strcmp(p.ip, "192.168.1.5") && !p.roms && !p.client_only);
}

static void test_parse_rejects(void) {
	const char* bad[] = {"", "garbage", "192.168.1.5:RR", "192.168.1.256", "1.2.3.4;reboot",
						 "1.2.3.4 :R", "::R", "999999999999999999999999999999999999999999999999999999999999999999", NULL};
	for (int i = 0; bad[i]; i++) {
		SyncPeer p;
		assert(!sync_parse_payload(bad[i], strlen(bad[i]), &p));
		assert(p.ip[0] == '\0' && !p.roms && !p.filters);
	}
}

static void test_format_roundtrip(void) {
	char buf[128];
	int n = sync_format_payload(buf, sizeof(buf), "HELLO_TRIMUI_SYNC:", "192.168.1.7", true);
	assert(n == (int)strlen("HELLO_TRIMUI_SYNC:192.168.1.7:R") + 3);
	// What old firmware sees: the C string only.
	assert(!strcmp(buf, "HELLO_TRIMUI_SYNC:192.168.1.7:R"));
	SyncPeer p;
	const char* pay = buf + strlen("HELLO_TRIMUI_SYNC:");
	assert(sync_parse_payload(pay, n - (pay - buf), &p));
	assert(!strcmp(p.ip, "192.168.1.7") && p.roms && !p.client_only && p.filters);
	n = sync_format_payload(buf, sizeof(buf), "", "10.99.0.2", false);
	assert(sync_parse_payload(buf, n, &p) && !p.roms && p.filters);
	// A wrong trailer is not a filtering peer.
	assert(sync_parse_payload("1.2.3.4:0\0v3", 12, &p) && !p.filters);
	assert(sync_format_payload(buf, 12, "", "192.168.1.7", false) == -1);
}

static void test_roles(void) {
	const char* lo = "10.99.0.1";
	const char* hi = "10.99.0.2";
	// neither client-only: lower IP serves
	assert(sync_is_server(false, false, lo, hi) && !sync_is_server(false, false, hi, lo));
	// both client-only: same IP rule
	assert(sync_is_server(true, true, lo, hi) && !sync_is_server(true, true, hi, lo));
	// peer client-only: we serve whatever the IPs
	assert(sync_is_server(false, true, lo, hi) && sync_is_server(false, true, hi, lo));
	// we are client-only: we never serve
	assert(!sync_is_server(true, false, lo, hi) && !sync_is_server(true, false, hi, lo));
}

static void test_cable_net(void) {
	assert(sync_ip_is_cable("10.99.0.1") && sync_ip_is_cable("10.99.0.2") && sync_ip_is_cable("10.99.0.3"));
	assert(!sync_ip_is_cable("10.99.0.4") && !sync_ip_is_cable("192.168.1.2") && !sync_ip_is_cable("x"));
}

static void test_filter_strings(void) {
	char buf[1024];
	assert(sync_shared_daemon_filter(buf, sizeof(buf)) > 0);
	const char* d0 = "- ._* - .DS_Store - /music-player/ ";
	assert(!strncmp(buf, d0, strlen(d0)));
	assert(strstr(buf, "+ /.minui/ + /.minui/*/ + /.minui/*/** - /.* + /*-*/ + /*-*/** - *"));
	assert(buf[strlen(buf) - 1] == '*');
	assert(sync_shared_client_args(buf, sizeof(buf)) > 0);
	const char* c0 = "--filter='- ._*' --filter='- .DS_Store' --filter='- /music-player/' ";
	assert(!strncmp(buf, c0, strlen(c0)));
	assert(strstr(buf, "--filter='- *'"));
	assert(sync_shared_client_args(buf, 20) == -1);
}

int main(void) {
	test_parse_flags();
	test_parse_rejects();
	test_format_roundtrip();
	test_roles();
	test_cable_net();
	test_filter_strings();
	printf("sync_proto: all tests passed\n");
	return 0;
}
