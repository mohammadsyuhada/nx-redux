// Host-compiled unit test for updater_url.c (no device toolchain).
// Build & run:
//   cc -I. updater_url.c tests/test_updater_url.c -o /tmp/test_updater_url && /tmp/test_updater_url
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../updater_url.h"

int main(void) {
	char out[512];

	// --- updater_parse_tag_from_location ---

	// Normal redirect target -> tag
	assert(updater_parse_tag_from_location(
			   "https://github.com/mohammadsyuhada/nx-redux/releases/tag/v1.8.0",
			   out, sizeof(out)) == 0);
	assert(strcmp(out, "v1.8.0") == 0);

	// Trailing CR/LF/space from a raw header line is stripped
	assert(updater_parse_tag_from_location(
			   "https://github.com/mohammadsyuhada/nx-redux/releases/tag/v1.10.2\r\n",
			   out, sizeof(out)) == 0);
	assert(strcmp(out, "v1.10.2") == 0);

	// Repo with no releases redirects elsewhere -> no /releases/tag/ segment
	assert(updater_parse_tag_from_location(
			   "https://github.com/mohammadsyuhada/nx-redux/releases",
			   out, sizeof(out)) == -1);

	// Empty tag after the segment
	assert(updater_parse_tag_from_location(
			   "https://github.com/mohammadsyuhada/nx-redux/releases/tag/",
			   out, sizeof(out)) == -1);

	// Tag longer than the output buffer must fail, not silently truncate
	// (a truncated tag would build a wrong download URL)
	assert(updater_parse_tag_from_location(
			   "https://github.com/mohammadsyuhada/nx-redux/releases/tag/v1.8.0",
			   out, 4) == -1);

	// NULL / empty input
	assert(updater_parse_tag_from_location(NULL, out, sizeof(out)) == -1);
	assert(updater_parse_tag_from_location("", out, sizeof(out)) == -1);

	// --- updater_parse_location_header ---

	// wget -S writes server headers to stderr with leading whitespace
	assert(updater_parse_location_header(
			   "--2026-08-31 12:00:00--  https://github.com/x/y/releases/latest\n"
			   "  HTTP/1.1 302 Found\n"
			   "  Server: GitHub.com\n"
			   "  Location: https://github.com/mohammadsyuhada/nx-redux/releases/tag/v1.8.0\n"
			   "  Content-Length: 0\n",
			   out, sizeof(out)) == 0);
	assert(strcmp(out, "https://github.com/mohammadsyuhada/nx-redux/releases/tag/v1.8.0") == 0);

	// Case-insensitive header name, last Location wins
	assert(updater_parse_location_header(
			   "  location: https://example.com/first\n"
			   "  LOCATION: https://example.com/second\r\n",
			   out, sizeof(out)) == 0);
	assert(strcmp(out, "https://example.com/second") == 0);

	// Real wget output: besides the indented -S header dump, wget prints its
	// OWN unindented "Location: <url> [following]" status line — the value
	// must be cut at the first whitespace (URLs cannot contain spaces).
	// Seen live on Brick 2026-08-31: the tag parsed as "v1.8.0 [following]".
	assert(updater_parse_location_header(
			   "  HTTP/1.1 302 Found\n"
			   "  Location: https://github.com/mohammadsyuhada/nx-redux/releases/tag/v1.8.0\n"
			   "  Content-Length: 0\n"
			   "Location: https://github.com/mohammadsyuhada/nx-redux/releases/tag/v1.8.0 [following]\n"
			   "0 redirections exceeded.\n",
			   out, sizeof(out)) == 0);
	assert(strcmp(out, "https://github.com/mohammadsyuhada/nx-redux/releases/tag/v1.8.0") == 0);

	// "Location:" appearing mid-line (not a header) is ignored
	assert(updater_parse_location_header(
			   "  X-Note: see Location: nowhere\n",
			   out, sizeof(out)) == -1);

	// No Location header at all (e.g. plain 200 or 404)
	assert(updater_parse_location_header(
			   "  HTTP/1.1 404 Not Found\n  Server: GitHub.com\n",
			   out, sizeof(out)) == -1);

	// Empty Location value
	assert(updater_parse_location_header("  Location: \n", out, sizeof(out)) == -1);

	assert(updater_parse_location_header(NULL, out, sizeof(out)) == -1);

	// --- updater_build_fallback_url ---

	assert(updater_build_fallback_url("mohammadsyuhada", "nx-redux", "v1.9.0", "brick",
									  out, sizeof(out)) == 0);
	assert(strcmp(out, "https://github.com/mohammadsyuhada/nx-redux/releases/download/"
					   "v1.9.0/NXRedux-v1.9.0-brick.zip") == 0);

	assert(updater_build_fallback_url("mohammadsyuhada", "nx-redux", "v1.9.0", "smartpros",
									  out, sizeof(out)) == 0);
	assert(strcmp(out, "https://github.com/mohammadsyuhada/nx-redux/releases/download/"
					   "v1.9.0/NXRedux-v1.9.0-smartpros.zip") == 0);

	// Truncation must fail, not return a partial URL
	assert(updater_build_fallback_url("mohammadsyuhada", "nx-redux", "v1.9.0", "brick",
									  out, 32) == -1);

	// Missing pieces fail
	assert(updater_build_fallback_url("o", "r", "", "brick", out, sizeof(out)) == -1);
	assert(updater_build_fallback_url("o", "r", NULL, "brick", out, sizeof(out)) == -1);

	printf("test_updater_url: all tests passed\n");
	return 0;
}
