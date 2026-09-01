#ifndef __DESKTOP_PROBE_H__
#define __DESKTOP_PROBE_H__
// Non-blocking TCP reachability probe. Returns 1 if a connection to host:port
// completes within timeout_ms, else 0. SDL-free, host-testable.
int desktop_probe_reachable(const char* host, int port, int timeout_ms);
#endif
