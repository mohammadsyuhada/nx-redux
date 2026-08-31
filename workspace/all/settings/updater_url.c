#include "updater_url.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

int updater_parse_tag_from_location(const char* location, char* out, size_t out_size) {
	if (!location || !out || out_size == 0)
		return -1;

	static const char segment[] = "/releases/tag/";
	const char* tag = strstr(location, segment);
	if (!tag)
		return -1;
	tag += sizeof(segment) - 1;

	size_t len = strlen(tag);
	while (len > 0 && isspace((unsigned char)tag[len - 1]))
		len--;

	if (len == 0 || len >= out_size)
		return -1;

	memcpy(out, tag, len);
	out[len] = '\0';
	return 0;
}

int updater_parse_location_header(const char* headers_text, char* out, size_t out_size) {
	if (!headers_text || !out || out_size == 0)
		return -1;

	int found = 0;
	const char* line = headers_text;
	while (line && *line) {
		const char* p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncasecmp(p, "Location:", 9) == 0) {
			p += 9;
			while (*p == ' ' || *p == '\t')
				p++;
			// A URL cannot contain whitespace: cut there, which also drops
			// the " [following]" wget appends to its own status line.
			size_t len = strcspn(p, " \t\r\n");
			if (len > 0 && len < out_size) {
				memcpy(out, p, len);
				out[len] = '\0';
				found = 1;
			}
		}
		line = strchr(line, '\n');
		if (line)
			line++;
	}

	return found ? 0 : -1;
}

int updater_build_fallback_url(const char* owner, const char* repo,
							   const char* tag, const char* device,
							   char* out, size_t out_size) {
	if (!owner || !*owner || !repo || !*repo || !tag || !*tag ||
		!device || !*device || !out || out_size == 0)
		return -1;

	int n = snprintf(out, out_size,
					 "https://github.com/%s/%s/releases/download/%s/NXRedux-%s-%s.zip",
					 owner, repo, tag, tag, device);
	if (n < 0 || (size_t)n >= out_size) {
		out[0] = '\0';
		return -1;
	}
	return 0;
}
