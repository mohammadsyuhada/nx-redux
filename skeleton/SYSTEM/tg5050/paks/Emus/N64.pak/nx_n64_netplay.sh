# Sourced by N64.pak's launch.sh. N64 netplay is mupen64plus' own netplay,
# run inside the core (patch 0002) against our relay (m64p-server.elf): the
# wizard (netplay.elf) finds the other players; the host starts the relay on
# this device and both sides hand the session to the core in NX_M64P_*, and
# to minarch in NX_CORE_NETPLAY (never NETPLAY_ROLE: that starts minarch's
# own lockstep engine). The host's saves travel inside the core's protocol;
# the client plays on them in memory and its own .srm is never written
# (NETPLAY_SAVES_DIR). A cancelled or failed wizard exits launch.sh.
# NETPLAY_LAUNCH_FLAG, NETPLAY_SESSION_FILE, NX_N64_NETPLAY_TMP and
# NX_N64_SERVER are overridable for tests only.
nx_n64_netplay() {
	NP_FLAG="${NETPLAY_LAUNCH_FLAG:-/tmp/netplay_launch}"
	[ -f "$NP_FLAG" ] || return 0
	rm -f "$NP_FLAG"
	NP_SESSION="${NETPLAY_SESSION_FILE:-/tmp/netplay_session}"
	NP_TMP="${NX_N64_NETPLAY_TMP:-/tmp}"
	NP_PORT=55445
	NP_STEM=$(basename "$ROM"); NP_STEM="${NP_STEM%.*}"

	netplay.elf --game "$NP_STEM" --max-players 4 --session-file "$NP_SESSION" \
		> "$LOGS_PATH/netplay-wizard.txt" 2>&1 || { nx_n64_netplay_cleanup; exit 0; }
	[ -f "$NP_SESSION" ] || { nx_n64_netplay_cleanup; exit 0; }
	. "$NP_SESSION"
	NP_ROLE="$NETPLAY_ROLE"; NP_PEER="$NETPLAY_PEER_IP"
	NP_PLAYER="${NETPLAY_PLAYER:-1}"; NP_NUM="${NETPLAY_NUM_PLAYERS:-2}"
	unset NETPLAY_ROLE NETPLAY_PEER_IP NETPLAY_MODE NETPLAY_PLAYER NETPLAY_NUM_PLAYERS NETPLAY_PEER_CAPS

	if [ "$NP_ROLE" = host ]; then
		# the relay runs on cpu2-3 (little cluster), offline by default here
		echo 1 >"${NX_N64_SYSFS_ROOT:-/sys}/devices/system/cpu/cpu2/online" 2>/dev/null
		echo 1 >"${NX_N64_SYSFS_ROOT:-/sys}/devices/system/cpu/cpu3/online" 2>/dev/null
		# the relay ships inside the pak (not on PATH); overridable for tests only
		"${NX_N64_SERVER:-$CORES_PATH/m64p-server.elf}" --port "$NP_PORT" --players "$NP_NUM" --buffer-target 2 \
			> "$LOGS_PATH/n64-netplay-server.txt" 2>&1 &
		NP_SERVER_PID=$!
		taskset -p 0xc "$NP_SERVER_PID" >/dev/null 2>&1 || true
		NP_SERVER_IP=127.0.0.1
	else
		NP_SERVER_IP="$NP_PEER"
		mkdir -p "$NP_TMP/n64-netplay-saves"
		export NETPLAY_SAVES_DIR="$NP_TMP/n64-netplay-saves"
	fi
	export NX_CORE_NETPLAY=1 NX_M64P_NETPLAY_HOST="$NP_SERVER_IP" NX_M64P_NETPLAY_PORT="$NP_PORT" NX_M64P_NETPLAY_PLAYER="$NP_PLAYER"
}

# After the game (or a cancelled wizard): stop the relay, the wizard's
# teardown (hotspot/WiFi restore), the client's working saves.
nx_n64_netplay_cleanup() {
	NP_TMP="${NX_N64_NETPLAY_TMP:-/tmp}"
	[ -n "${NP_SERVER_PID:-}" ] && kill "$NP_SERVER_PID" 2>/dev/null
	if [ -f "${NETPLAY_SESSION_FILE:-/tmp/netplay_session}" ]; then
		netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
	fi
	rm -rf "$NP_TMP/n64-netplay-saves"
}
