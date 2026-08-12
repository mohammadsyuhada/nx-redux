#define _GNU_SOURCE
#include "stream_probe.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Total time budget for the probe (curl -m). Kept short so a dead host fails
// fast instead of black-screening in ffplay.
#define PROBE_TIMEOUT_SECS 6

bool Stream_probeReachable(const char* url) {
	if (!url || !url[0])
		return false;

	// Only probe http(s) URLs through the shell. The URL is third-party data
	// (iptv-org playlists), so anything else (rtmp://, file://, a leading '-')
	// is not handed to curl; it's left for ffplay's exec-based path (no shell)
	// -> fail open. This, the single-quoting below, and the `--` end-of-options
	// marker together prevent shell and curl-argument injection.
	if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
		return true;

	// Escape single quotes so the URL can sit inside a single-quoted shell arg
	// ('\'' = close-quote, literal quote, reopen-quote).
	char esc[2048];
	size_t j = 0;
	for (size_t i = 0; url[i] && j + 4 < sizeof(esc); i++) {
		if (url[i] == '\'') {
			esc[j++] = '\'';
			esc[j++] = '\\';
			esc[j++] = '\'';
			esc[j++] = '\'';
		} else {
			esc[j++] = url[i];
		}
	}
	esc[j] = '\0';

	// -k: firmware has no CA bundle; -L: follow redirects; -r 0-0: first byte only
	// (don't pull a whole live stream); -o /dev/null -w %{http_code}: print status.
	char cmd[2400];
	// `--` marks the end of options so a URL beginning with '-' can never be
	// interpreted as a curl flag (argument injection).
	snprintf(cmd, sizeof(cmd),
			 "curl -s -k -L -m %d -o /dev/null -w '%%{http_code}' -r 0-0 -- '%s' 2>/dev/null",
			 PROBE_TIMEOUT_SECS, esc);

	FILE* fp = popen(cmd, "r");
	if (!fp)
		return true; // can't even spawn the probe -> attempt playback

	char out[16] = {0};
	char* got = fgets(out, sizeof(out), fp);
	pclose(fp);

	// No output at all (e.g. curl not installed) -> fail open, attempt playback.
	if (!got)
		return true;

	int code = atoi(out);
	if (code >= 200 && code < 400)
		return true; // reachable
	// 4xx/5xx = blocked/missing; 000 = connection/DNS failure -> unreachable.
	return false;
}
