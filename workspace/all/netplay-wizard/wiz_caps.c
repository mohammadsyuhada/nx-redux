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
