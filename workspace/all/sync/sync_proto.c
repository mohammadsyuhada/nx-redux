#include "sync_proto.h"

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Folders that match the state patterns below but hold settings, caches or
// per-device data: app settings (music/media player), PortMaster's HOME,
// per-device emulator configs, the art cache, the DC netplay save backup, and
// what the retired standalone N64 and Dreamcast emulators left behind.
const char* const sync_shared_rules[] = {
	"- ._*",
	"- .DS_Store",
	"- /music-player/",
	"- /media-player/",
	"- /PORTS-portmaster/",
	"- /*-*/config/",
	"- /DC-flycast/*/", // the core's states sit at the top; folders are the standalone's and netplay's
	"- /DC-flycast/nx-seeded.txt",
	"- /N64-mupen64plus/",
	"- /.minui/placeholders/",
	"+ /.minui/",
	"+ /.minui/*/",
	"+ /.minui/*/**",
	// rsync's * matches a leading dot: no other dot-folder passes as a state one.
	"- /.*",
	"+ /*-*/",
	"+ /*-*/**",
	"- *",
	NULL,
};

bool sync_parse_payload(const char* buf, size_t len, SyncPeer* out) {
	memset(out, 0, sizeof(*out));
	size_t slen = strnlen(buf, len);

	char text[64];
	if (slen >= sizeof(text))
		return false;
	memcpy(text, buf, slen);
	text[slen] = '\0';

	char flag = '\0';
	char* colon = strrchr(text, ':');
	if (colon && strlen(colon + 1) <= 1) {
		flag = colon[1];
		*colon = '\0';
	}
	out->roms = (flag == 'R' || flag == 'C');
	out->client_only = (flag == 'c' || flag == 'C');

	size_t tlen = strlen(SYNC_PAYLOAD_TRAILER);
	if (slen < len && len - slen - 1 == tlen && !memcmp(buf + slen + 1, SYNC_PAYLOAD_TRAILER, tlen))
		out->filters = true;
	if (out->client_only)
		out->filters = true;

	struct in_addr addr;
	if (strlen(text) >= sizeof(out->ip) || inet_pton(AF_INET, text, &addr) != 1) {
		memset(out, 0, sizeof(*out));
		return false;
	}
	strcpy(out->ip, text);
	return true;
}

int sync_format_payload(char* out, size_t size, const char* prefix, const char* ip, bool roms) {
	int n = snprintf(out, size, "%s%s:%s", prefix, ip, roms ? "R" : "0");
	if (n < 0)
		return -1;
	size_t tlen = strlen(SYNC_PAYLOAD_TRAILER);
	if ((size_t)n + 1 + tlen > size)
		return -1;
	memcpy(out + n + 1, SYNC_PAYLOAD_TRAILER, tlen);
	return n + 1 + (int)tlen;
}

static uint32_t ip_host_order(const char* ip) {
	struct in_addr a;
	if (inet_pton(AF_INET, ip, &a) != 1)
		return 0;
	return ntohl(a.s_addr);
}

bool sync_is_server(bool own_client_only, bool peer_client_only, const char* own_ip, const char* peer_ip) {
	if (peer_client_only != own_client_only)
		return peer_client_only;
	return ip_host_order(own_ip) < ip_host_order(peer_ip);
}

bool sync_ip_is_cable(const char* ip) {
	struct in_addr a;
	if (inet_pton(AF_INET, ip, &a) != 1)
		return false;
	return (ntohl(a.s_addr) & 0xfffffffc) == ip_host_order(SYNC_CABLE_NET);
}

static int join_rules(char* out, size_t size, const char* before, const char* after) {
	size_t used = 0;
	out[0] = '\0';
	for (int i = 0; sync_shared_rules[i]; i++) {
		int n = snprintf(out + used, size - used, "%s%s%s%s", used ? " " : "", before,
						 sync_shared_rules[i], after);
		if (n < 0 || (size_t)n >= size - used)
			return -1;
		used += n;
	}
	return (int)used;
}

int sync_shared_client_args(char* out, size_t size) {
	return join_rules(out, size, "--filter='", "'");
}

int sync_shared_daemon_filter(char* out, size_t size) {
	return join_rules(out, size, "", "");
}
