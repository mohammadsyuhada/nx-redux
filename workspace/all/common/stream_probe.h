#ifndef __STREAM_PROBE_H__
#define __STREAM_PROBE_H__

#include <stdbool.h>

// Quick reachability check for a stream URL before handing off to ffplay.
// Uses a short-timeout, single-byte ranged curl GET (works for HLS playlists and
// avoids downloading a live stream). Returns true if playback should be attempted
// (HTTP 2xx/3xx, or the check couldn't run — fail-open); false only when the
// stream is definitively unreachable (HTTP 4xx/5xx, or a connection/DNS failure
// that yields no HTTP response).
bool Stream_probeReachable(const char* url);

#endif // __STREAM_PROBE_H__
