#!/bin/sh
# netplay-prelaunch.sh - shared pre-launch netplay wizard step for minarch paks.
#
# Source (don't exec) from a pak's launch.sh after ROM="$1" is set, before the
# minarch.elf line:
#     . "$SHARED_SYSTEM_PATH/bin/netplay-prelaunch.sh"
# Pair it with a teardown block after minarch.elf exits:
#     if [ -f /tmp/netplay_session ]; then
#         netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
#     fi
#
# Plain launches (no /tmp/netplay_launch flag): no-op.
# Netplay launches: consume the flag, run the wizard (rendezvous only - no
# save-sync args; minarch cores keep their own save handling), and on success
# export NETPLAY_ROLE / NETPLAY_PEER_IP / NETPLAY_MODE for minarch's boot-time
# engine start. Env vars on purpose: /tmp/netplay_session can survive an
# OSD/power-quit, env cannot, so a stale session file never starts netplay.
# On wizard cancel/error, exit the SOURCING launch.sh with status 0 - MinUI
# restarts nextui and the user is back at the game list; the emulator never
# starts (and never falls through to a peerless single-player launch).
#
# NETPLAY_LAUNCH_FLAG / NETPLAY_SESSION_FILE are overridable for tests only.
NETPLAY_LAUNCH_FLAG="${NETPLAY_LAUNCH_FLAG:-/tmp/netplay_launch}"
NETPLAY_SESSION_FILE="${NETPLAY_SESSION_FILE:-/tmp/netplay_session}"

if [ -f "$NETPLAY_LAUNCH_FLAG" ]; then
	rm -f "$NETPLAY_LAUNCH_FLAG"
	NETPLAY_GAME_NAME=$(basename "$ROM")
	NETPLAY_GAME_NAME="${NETPLAY_GAME_NAME%.*}"
	netplay.elf --game "$NETPLAY_GAME_NAME" --session-file "$NETPLAY_SESSION_FILE" \
		> "$LOGS_PATH/netplay-wizard.txt" 2>&1
	if [ $? -ne 0 ]; then
		exit 0
	fi
	[ -f "$NETPLAY_SESSION_FILE" ] || exit 0
	. "$NETPLAY_SESSION_FILE"
	export NETPLAY_ROLE NETPLAY_PEER_IP NETPLAY_MODE
fi
