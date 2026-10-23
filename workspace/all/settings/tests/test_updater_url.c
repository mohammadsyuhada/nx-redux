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
			   "https://github.com/nx-redux/nx-handheld/releases/tag/v1.8.0",
			   out, sizeof(out)) == 0);
	assert(strcmp(out, "v1.8.0") == 0);

	// Trailing CR/LF/space from a raw header line is stripped
	assert(updater_parse_tag_from_location(
			   "https://github.com/nx-redux/nx-handheld/releases/tag/v1.10.2\r\n",
			   out, sizeof(out)) == 0);
	assert(strcmp(out, "v1.10.2") == 0);

	// Repo with no releases redirects elsewhere -> no /releases/tag/ segment
	assert(updater_parse_tag_from_location(
			   "https://github.com/nx-redux/nx-handheld/releases",
			   out, sizeof(out)) == -1);

	// Empty tag after the segment
	assert(updater_parse_tag_from_location(
			   "https://github.com/nx-redux/nx-handheld/releases/tag/",
			   out, sizeof(out)) == -1);

	// Tag longer than the output buffer must fail, not silently truncate
	// (a truncated tag would build a wrong download URL)
	assert(updater_parse_tag_from_location(
			   "https://github.com/nx-redux/nx-handheld/releases/tag/v1.8.0",
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
			   "  Location: https://github.com/nx-redux/nx-handheld/releases/tag/v1.8.0\n"
			   "  Content-Length: 0\n",
			   out, sizeof(out)) == 0);
	assert(strcmp(out, "https://github.com/nx-redux/nx-handheld/releases/tag/v1.8.0") == 0);

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
			   "  Location: https://github.com/nx-redux/nx-handheld/releases/tag/v1.8.0\n"
			   "  Content-Length: 0\n"
			   "Location: https://github.com/nx-redux/nx-handheld/releases/tag/v1.8.0 [following]\n"
			   "0 redirections exceeded.\n",
			   out, sizeof(out)) == 0);
	assert(strcmp(out, "https://github.com/nx-redux/nx-handheld/releases/tag/v1.8.0") == 0);

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

	assert(updater_build_fallback_url("nx-redux", "nx-handheld", "v1.9.0", "brick",
									  out, sizeof(out)) == 0);
	assert(strcmp(out, "https://github.com/nx-redux/nx-handheld/releases/download/"
					   "v1.9.0/NXRedux-v1.9.0-brick.zip") == 0);

	assert(updater_build_fallback_url("nx-redux", "nx-handheld", "v1.9.0", "smartpros",
									  out, sizeof(out)) == 0);
	assert(strcmp(out, "https://github.com/nx-redux/nx-handheld/releases/download/"
					   "v1.9.0/NXRedux-v1.9.0-smartpros.zip") == 0);

	// Truncation must fail, not return a partial URL
	assert(updater_build_fallback_url("nx-redux", "nx-handheld", "v1.9.0", "brick",
									  out, 32) == -1);

	// Missing pieces fail
	assert(updater_build_fallback_url("o", "r", "", "brick", out, sizeof(out)) == -1);
	assert(updater_build_fallback_url("o", "r", NULL, "brick", out, sizeof(out)) == -1);

	// --- updater_find_zip_asset_url ---

	// GitHub's asset object order: name, uploader{...}, digest, then browser_download_url.
	static const char rel[] =
		"{\"tag_name\":\"v2.3.0\",\"assets\":["
		"{\"name\":\"NXRedux-v2.3.0-brickpro.zip\",\"uploader\":{\"id\":1},"
		"\"digest\":\"sha256:AAAA1111\",\"browser_download_url\": "
		"\"https://x/NXRedux-v2.3.0-brickpro.zip\"},"
		"{\"name\":\"NXRedux-v2.3.0-brick.zip\",\"uploader\":{\"id\":1},"
		"\"digest\":\"sha256:BBBB2222\",\"browser_download_url\":\"https://x/NXRedux-v2.3.0-brick.zip\"},"
		"{\"name\":\"NXRedux-v2.3.0-smartpros.zip\",\"uploader\":{\"id\":1},"
		"\"browser_download_url\":\"https://x/NXRedux-v2.3.0-smartpros.zip\"},"
		"{\"name\":\"NXRedux-v2.3.0-smartpro.zip\",\"uploader\":{\"id\":1},"
		"\"browser_download_url\":\"https://x/NXRedux-v2.3.0-smartpro.zip\"}"
		"]}";
	char digest[80];

	// "-brick.zip" must not match the earlier "-brickpro.zip"; digest comes from the same asset.
	assert(updater_find_zip_asset_url(rel, "brick.zip", out, sizeof(out), digest, sizeof(digest)) == out);
	assert(strcmp(out, "https://x/NXRedux-v2.3.0-brick.zip") == 0);
	assert(strcmp(digest, "bbbb2222") == 0);

	// A space after the colon is accepted.
	assert(updater_find_zip_asset_url(rel, "brickpro.zip", out, sizeof(out), digest, sizeof(digest)) == out);
	assert(strcmp(out, "https://x/NXRedux-v2.3.0-brickpro.zip") == 0);
	assert(strcmp(digest, "aaaa1111") == 0);

	// "-smartpro.zip" is not a suffix of "-smartpros.zip"; neither asset has a digest, and the
	// backward scan stops at the '}' closing the previous asset instead of taking its digest.
	assert(updater_find_zip_asset_url(rel, "smartpro.zip", out, sizeof(out), digest, sizeof(digest)) == out);
	assert(strcmp(out, "https://x/NXRedux-v2.3.0-smartpro.zip") == 0);
	assert(strcmp(digest, "") == 0);
	assert(updater_find_zip_asset_url(rel, "smartpros.zip", out, sizeof(out), digest, sizeof(digest)) == out);
	assert(strcmp(out, "https://x/NXRedux-v2.3.0-smartpros.zip") == 0);
	assert(strcmp(digest, "") == 0);

	// The digest is cut to digest_size; the URL to out_size.
	assert(updater_find_zip_asset_url(rel, "brick.zip", out, 10, digest, 5) == out);
	assert(strcmp(out, "https://x") == 0);
	assert(strcmp(digest, "bbbb") == 0);

	// A NULL digest buffer is allowed.
	assert(updater_find_zip_asset_url(rel, "brick.zip", out, sizeof(out), NULL, 0) == out);

	// No match, no assets array, or bad args: NULL and an empty digest.
	strcpy(digest, "stale");
	assert(updater_find_zip_asset_url(rel, "trimui.zip", out, sizeof(out), digest, sizeof(digest)) == NULL);
	assert(strcmp(digest, "") == 0);
	assert(updater_find_zip_asset_url("{\"browser_download_url\":\"https://x/a-brick.zip\"}", "brick.zip", out,
									  sizeof(out), NULL, 0) == NULL);
	assert(updater_find_zip_asset_url(NULL, "brick.zip", out, sizeof(out), NULL, 0) == NULL);
	assert(updater_find_zip_asset_url(rel, "brick.zip", out, 0, NULL, 0) == NULL);

	// --- updater_extract_first_paragraph ---

	char notes[64];
	updater_extract_first_paragraph("## What's new\\nFaster menus\\n\\nSecond paragraph", notes, sizeof(notes));
	assert(strcmp(notes, "What's new Faster menus") == 0);

	// CRLF breaks: a lone \r\n joins lines, \r\n\r\n ends the paragraph.
	updater_extract_first_paragraph("Line one\\r\\nline two\\r\\n\\r\\nmore", notes, sizeof(notes));
	assert(strcmp(notes, "Line one line two") == 0);

	// Trailing whitespace is trimmed; '#' runs are dropped mid-text too.
	updater_extract_first_paragraph("Fixes ### for #1   \\n", notes, sizeof(notes));
	assert(strcmp(notes, "Fixes for 1") == 0);

	// Output is cut to out_size.
	updater_extract_first_paragraph("abcdefghij", notes, 5);
	assert(strcmp(notes, "abcd") == 0);

	updater_extract_first_paragraph("", notes, sizeof(notes));
	assert(strcmp(notes, "") == 0);
	updater_extract_first_paragraph(NULL, notes, sizeof(notes));

	printf("test_updater_url: all tests passed\n");
	return 0;
}
