// wiz_caps.c - see wiz_caps.h.

#include "wiz_caps.h"

#include <stdio.h>
#include <string.h>

bool WizCaps_isValid(const char* token) {
	if (!token || !token[0])
		return false;
	size_t n = 0;
	for (const char* c = token; *c; c++, n++) {
		if (n >= WIZ_CAPS_MAX - 1)
			return false;
		// explicit ranges, not isalnum() (locale-dependent), like wiz_name_is_safe
		bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
				  *c == '.' || *c == '_' || *c == '=' || *c == ',' || *c == '-';
		if (!ok)
			return false;
	}
	return true;
}

void WizCaps_find(const char* line, char* out, size_t out_size) {
	if (!out || !out_size)
		return;
	out[0] = '\0';
	if (!line)
		return;
	// skip the fixed fields: HELLO <version> <game> <role>
	const char* p = line;
	for (int field = 0; field < 4; field++) {
		while (*p == ' ')
			p++;
		if (!*p)
			return;
		while (*p && *p != ' ')
			p++;
	}
	while (*p) {
		while (*p == ' ')
			p++;
		const char* end = p;
		while (*end && *end != ' ' && *end != '\n' && *end != '\r')
			end++;
		if (end - p > 5 && strncmp(p, "caps=", 5) == 0) {
			char token[WIZ_CAPS_MAX + 1];
			size_t len = (size_t)(end - p - 5);
			if (len >= sizeof(token))
				return; // too long to be valid
			memcpy(token, p + 5, len);
			token[len] = '\0';
			if (WizCaps_isValid(token))
				snprintf(out, out_size, "%s", token);
			return;
		}
		if (!*end || *end == '\n' || *end == '\r')
			return;
		p = end;
	}
}

void WizCaps_field(const char* token, char* out, size_t out_size) {
	if (!out || !out_size)
		return;
	if (WizCaps_isValid(token))
		snprintf(out, out_size, " caps=%s", token);
	else
		out[0] = '\0';
}

void WizCaps_value(const char* caps, const char* key, char* out, size_t out_size) {
	if (!out || !out_size)
		return;
	out[0] = '\0';
	if (!caps || !key)
		return;
	size_t key_len = strlen(key);
	for (const char* p = caps; *p;) {
		const char* end = strchr(p, ',');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		if (len > key_len + 1 && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
			snprintf(out, out_size, "%.*s", (int)(len - key_len - 1), p + key_len + 1);
			return;
		}
		if (!end)
			break;
		p = end + 1;
	}
}

bool WizCaps_coreMismatch(const char* ours, const char* theirs) {
	char a[WIZ_CAPS_MAX];
	char b[WIZ_CAPS_MAX];
	WizCaps_value(ours, "core", a, sizeof(a));
	WizCaps_value(theirs, "core", b, sizeof(b));
	return a[0] && b[0] && strcmp(a, b) != 0;
}

const char* WizCaps_coreName(const char* core) {
	static const struct {
		const char* core;
		const char* name;
	} names[] = {
		{"gpsp", "gpSP"},
		{"mgba", "mGBA"},
		{"picodrive", "PicoDrive"},
		{"genesis_plus_gx", "Genesis Plus GX"},
		{"snes9x", "Snes9x"},
		{"mednafen_supafaust", "Supafaust"},
		{"pcsx_rearmed", "PCSX ReARMed"},
		{"swanstation", "SwanStation"},
		{"fbneo", "FBNeo"},
		{"fceumm", "FCEUmm"},
	};

	if (!core)
		return "";
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (strcmp(core, names[i].core) == 0)
			return names[i].name;
	return core;
}

void WizCaps_coreLabel(const char* core, const char* tag, char* out, size_t out_size) {
	if (!out || !out_size)
		return;
	if (tag && tag[0])
		snprintf(out, out_size, "%s (%s folder)", WizCaps_coreName(core), tag);
	else
		snprintf(out, out_size, "%s", WizCaps_coreName(core));
}

void WizCaps_coreReason(const char* caps, char* out, size_t out_size) {
	char core[WIZ_CAPS_MAX];
	char tag[WIZ_CAPS_MAX];

	if (!out || !out_size)
		return;
	WizCaps_value(caps, "core", core, sizeof(core));
	WizCaps_value(caps, "tag", tag, sizeof(tag));
	size_t core_len = strlen(core);
	size_t tag_len = strlen(tag);

	// "core-" + core [+ "." + tag] + terminator
	if (core_len && tag_len && 5 + core_len + 1 + tag_len < out_size)
		snprintf(out, out_size, "core-%s.%s", core, tag);
	else if (core_len && 5 + core_len < out_size)
		snprintf(out, out_size, "core-%s", core);
	else
		snprintf(out, out_size, "core");
}

void WizCaps_reasonLabel(const char* reason, char* out, size_t out_size) {
	char core[WIZ_CAPS_MAX];

	if (!out || !out_size)
		return;
	out[0] = '\0';
	if (!reason || strncmp(reason, "core-", 5) != 0 || !reason[5])
		return;
	snprintf(core, sizeof(core), "%s", reason + 5);
	// Core names carry no '.', so the first one starts the folder tag.
	char* dot = strchr(core, '.');
	const char* tag = "";
	if (dot) {
		*dot = '\0';
		tag = dot + 1;
	}
	WizCaps_coreLabel(core, tag, out, out_size);
}
