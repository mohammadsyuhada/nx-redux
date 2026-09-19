// Host-compiled unit test for ssh_desc.c (no device toolchain).
// Build & run:
//   cc -I. ssh_desc.c tests/test_ssh_desc.c -o /tmp/test_ssh_desc && /tmp/test_ssh_desc
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ssh_desc.h"

int main(void) {
	char b[128];

	// --- not running (ip/no_password are irrelevant) ---
	assert(dev_format_ssh_desc(b, sizeof(b), 0, 0, "192.168.1.8") >= 0);
	assert(strcmp(b, "Start SSH server for remote access.") == 0);
	assert(dev_format_ssh_desc(b, sizeof(b), 0, 1, "") >= 0);
	assert(strcmp(b, "Start SSH server for remote access.") == 0);

	// --- running, ip non-empty, password (tg5040 Brick / Brick Pro) ---
	dev_format_ssh_desc(b, sizeof(b), 1, 0, "192.168.1.8");
	assert(strcmp(b, "SSH active. ssh root@192.168.1.8  Password: tina") == 0);

	// --- running, ip non-empty, no password (tg5050 Smart Pro S) ---
	dev_format_ssh_desc(b, sizeof(b), 1, 1, "192.168.1.8");
	assert(strcmp(b, "SSH active. ssh root@192.168.1.8  No password") == 0);

	// --- running, ip empty, password ---
	dev_format_ssh_desc(b, sizeof(b), 1, 0, "");
	assert(strcmp(b, "SSH active. Login: root / tina (no network)") == 0);

	// --- running, ip empty, no password ---
	dev_format_ssh_desc(b, sizeof(b), 1, 1, "");
	assert(strcmp(b, "SSH active. Login: root, no password (no network)") == 0);

	// NULL ip is treated the same as an empty one (no network)
	dev_format_ssh_desc(b, sizeof(b), 1, 0, NULL);
	assert(strcmp(b, "SSH active. Login: root / tina (no network)") == 0);

	// --- truncation: a tiny buffer must not overflow and must report it ---
	char tiny[8];
	int n = dev_format_ssh_desc(tiny, sizeof(tiny), 1, 0, "192.168.1.8");
	assert(n >= (int)sizeof(tiny));			  // snprintf-style would-be length
	assert(strlen(tiny) == sizeof(tiny) - 1); // safely NUL-terminated

	// NULL / zero buffer
	assert(dev_format_ssh_desc(NULL, sizeof(b), 1, 0, "192.168.1.8") == -1);
	assert(dev_format_ssh_desc(b, 0, 1, 0, "192.168.1.8") == -1);

	// --- longest string still fits the 128-byte on-device buffer ---
	int longest = dev_format_ssh_desc(b, sizeof(b), 1, 0, "255.255.255.255");
	assert(longest > 0 && longest < (int)sizeof(b));

	printf("test_ssh_desc: all tests passed\n");
	return 0;
}
