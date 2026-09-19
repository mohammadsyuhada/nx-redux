/*
 * ssh_desc.c - Formatter for the Developer page's "Enable SSH" hint.
 *
 * Kept free of SDL/api.h so it can be unit-tested on the host
 * (see tests/test_ssh_desc.c).
 */

#include "ssh_desc.h"

#include <stdio.h>

int dev_format_ssh_desc(char* buf, size_t len, int running, int no_password, const char* ip) {
	if (!buf || len == 0)
		return -1;

	int has_ip = (ip && ip[0] != '\0');

	if (!running)
		return snprintf(buf, len, "Start SSH server for remote access.");

	if (has_ip) {
		if (no_password)
			return snprintf(buf, len, "SSH active. ssh root@%s  No password", ip);
		return snprintf(buf, len, "SSH active. ssh root@%s  Password: tina", ip);
	}

	if (no_password)
		return snprintf(buf, len, "SSH active. Login: root, no password (no network)");
	return snprintf(buf, len, "SSH active. Login: root / tina (no network)");
}
