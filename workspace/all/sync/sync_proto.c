#include "sync_proto.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

bool sync_names_add(SyncNames* l, const char* name) {
	if (l->count == l->cap) {
		int cap = l->cap ? l->cap * 2 : 16;
		char** items = realloc(l->items, cap * sizeof(*items));
		if (!items)
			return false;
		l->items = items;
		l->cap = cap;
	}
	char* copy = strdup(name);
	if (!copy)
		return false;
	l->items[l->count++] = copy;
	return true;
}

bool sync_names_has(const SyncNames* l, const char* name) {
	for (int i = 0; i < l->count; i++)
		if (!strcmp(l->items[i], name))
			return true;
	return false;
}

bool sync_names_has_ci(const SyncNames* l, const char* name) {
	for (int i = 0; i < l->count; i++)
		if (!strcasecmp(l->items[i], name))
			return true;
	return false;
}

void sync_names_free(SyncNames* l) {
	for (int i = 0; i < l->count; i++)
		free(l->items[i]);
	free(l->items);
	memset(l, 0, sizeof(*l));
}

bool sync_console_display(const char* folder, char* out, size_t size) {
	size_t len = strlen(folder);
	while (len && isspace((unsigned char)folder[len - 1]))
		len--;
	if (!len || folder[len - 1] != ')')
		return false;
	const char* open = NULL;
	for (const char* p = folder; p < folder + len - 1; p++)
		if (*p == '(')
			open = p;
	if (!open)
		return false;
	// The tag: something besides spaces, no parentheses.
	bool tag = false;
	for (const char* p = open + 1; p < folder + len - 1; p++) {
		if (*p == ')')
			return false;
		if (!isspace((unsigned char)*p))
			tag = true;
	}
	if (!tag)
		return false;
	const char* start = folder;
	const char* end = open;
	while (start < end && isspace((unsigned char)*start))
		start++;
	while (end > start && isspace((unsigned char)end[-1]))
		end--;
	if (end == start || (size_t)(end - start) >= size)
		return false;
	memcpy(out, start, end - start);
	out[end - start] = '\0';
	return true;
}

bool sync_is_sibling(const char* a, const char* b) {
	char da[256], db[256];
	return strcmp(a, b) != 0 && sync_console_display(a, da, sizeof(da)) && sync_console_display(b, db, sizeof(db)) &&
		   !strcmp(da, db);
}

bool sync_is_game_name(const char* name) {
	size_t len = strlen(name);
	size_t dlen = strlen(".disabled");
	if (!len || name[0] == '.' || !strcmp(name, "map.txt"))
		return false;
	return !(len >= dlen && !strcmp(name + len - dlen, ".disabled"));
}

// Advances *p past one run of non-space characters; its length.
static size_t list_token(const char** p) {
	const char* s = *p;
	while (**p && **p != ' ')
		(*p)++;
	return *p - s;
}

bool sync_list_line_name(const char* line, char* out, size_t size, bool* is_dir) {
	const char* p = line;
	const char* perms = p;
	if (list_token(&p) != 10 || !strchr("-dlcbps", perms[0]))
		return false;
	while (*p == ' ')
		p++;
	if (!list_token(&p)) // size
		return false;
	while (*p == ' ')
		p++;
	const char* date = p;
	if (list_token(&p) != 10 || date[4] != '/' || date[7] != '/' || *p != ' ')
		return false;
	const char* time = ++p;
	if (list_token(&p) != 8 || time[2] != ':' || time[5] != ':' || *p != ' ')
		return false;
	const char* name = ++p;
	size_t len = strcspn(name, "\r\n");
	if (perms[0] == 'l') {
		const char* arrow = strstr(name, " -> ");
		if (arrow && (size_t)(arrow - name) < len)
			len = arrow - name;
	}
	if (!len || (len == 1 && name[0] == '.') || len >= size)
		return false;
	memcpy(out, name, len);
	out[len] = '\0';
	*is_dir = perms[0] == 'd';
	return true;
}

int sync_escape_pattern(const char* pattern, char* out, size_t size) {
	bool wild = strpbrk(pattern, "*?[") != NULL;
	size_t used = 0;
	for (const char* p = pattern; *p; p++) {
		bool esc = wild && strchr("\\*?[", *p);
		if (used + esc + 1 >= size)
			return -1;
		if (esc)
			out[used++] = '\\';
		out[used++] = *p;
	}
	out[used] = '\0';
	return (int)used;
}

int sync_sibling_excludes(const char* folder, const SyncNames* sender_x, const SyncNames* receiver_x,
						  const SyncNames* receiver_siblings, SyncNames* out) {
	int added = 0;
	for (int i = 0; i < sender_x->count; i++) {
		const char* name = sender_x->items[i];
		if (!sync_is_game_name(name) || strpbrk(name, "\r\n"))
			continue;
		if (!sync_names_has_ci(receiver_siblings, name) || sync_names_has_ci(receiver_x, name))
			continue;
		char path[1024], pattern[2048];
		int n = snprintf(path, sizeof(path), "/%s/%s", folder, name);
		if (n < 0 || (size_t)n >= sizeof(path) || sync_escape_pattern(path, pattern, sizeof(pattern)) < 0)
			return -1;
		if (!sync_names_add(out, pattern))
			return -1;
		added++;
	}
	return added;
}
