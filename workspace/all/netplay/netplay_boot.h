#ifndef NETPLAY_BOOT_H
#define NETPLAY_BOOT_H

// Boot-time netplay engine start for wizard-launched sessions.
// Reads NETPLAY_ROLE / NETPLAY_PEER_IP / NETPLAY_MODE from the environment
// (exported by netplay-prelaunch.sh on the wizard-success path only).
// Returns 0 when no netplay launch was requested OR the engine started
// successfully; -1 when a requested netplay start failed (caller shows a
// failure message and exits without entering the main loop).
int NetplayBoot_startFromEnv(const char* core_name);

#endif
