#!/usr/bin/env bash
# Host test for N64 netplay in N64.pak: real launch.sh with netplay.elf,
# m64p-server.elf and minarch.elf stubbed on PATH.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"; kill $(cat "$TMP/server.pid" 2>/dev/null) 2>/dev/null || true' EXIT
FAIL=0; fail() { echo "FAIL: $*"; FAIL=1; }
mkdir -p "$TMP/bin" "$TMP/logs" "$TMP/tmp" "$TMP/sd/.userdata/shared" "$TMP/sd/.userdata/tg5040" "$TMP/sd/.userdata/tg5050" "$TMP/sd/Saves/N64"
export T="$TMP"
cat > "$TMP/bin/netplay.elf" <<'EOF'
#!/bin/sh
if [ "$1" = "--cleanup" ]; then echo cleanup >> "$T/cleanup"; exit 0; fi
printf '%s\n' "$@" > "$T/wizard_args"
prev=""; for a in "$@"; do [ "$prev" = "--session-file" ] && session="$a"; prev="$a"; done
[ "${NPELF_RC:-0}" = 0 ] || exit "$NPELF_RC"
printf 'NETPLAY_ROLE=%s\nNETPLAY_PEER_IP=10.0.0.2\nNETPLAY_PLAYER=%s\nNETPLAY_NUM_PLAYERS=2\n' "$NPELF_ROLE" "$NPELF_PLAYER" > "$session"
EOF
# The relay is NOT on PATH (it ships in the pak): the helper must call it by
# path, so the stub lives outside bin/ and is handed over via NX_N64_SERVER.
mkdir -p "$TMP/pak"
cat > "$TMP/pak/m64p-server.elf" <<'EOF'
#!/bin/sh
printf '%s\n' "$@" > "$T/server_args"; echo $$ > "$T/server.pid"; sleep 30
EOF
cat > "$TMP/bin/minarch.elf" <<'EOF'
#!/bin/sh
for v in NX_CORE_NETPLAY NX_M64P_NETPLAY_HOST NX_M64P_NETPLAY_PORT NX_M64P_NETPLAY_PLAYER NETPLAY_SAVES_DIR NETPLAY_ROLE; do eval "echo $v=\${$v-UNSET}"; done > "$T/minarch_env"
sleep 1 # a session lasts a while: the relay must be up before the cleanup stops it
EOF
chmod +x "$TMP/bin/"* "$TMP/pak/m64p-server.elf"
launch() { # $1 platform
	rm -f "$TMP/minarch_env" "$TMP/server_args" "$TMP/wizard_args" "$TMP/cleanup"
	touch "$TMP/tmp/netplay_launch"
	PATH="$TMP/bin:$PATH" SDCARD_PATH="$TMP/sd" SAVES_PATH="$TMP/sd/Saves" LOGS_PATH="$TMP/logs" \
		USERDATA_PATH="$TMP/sd/.userdata/$1" SHARED_USERDATA_PATH="$TMP/sd/.userdata/shared" \
		NETPLAY_LAUNCH_FLAG="$TMP/tmp/netplay_launch" NETPLAY_SESSION_FILE="$TMP/tmp/netplay_session" \
		NX_N64_NETPLAY_TMP="$TMP/tmp" NX_N64_SYSFS_ROOT="$TMP/sys" NX_N64_SERVER="$TMP/pak/m64p-server.elf" \
		sh "$PWD/skeleton/SYSTEM/$1/paks/Emus/N64.pak/launch.sh" "$TMP/sd/Roms/Nintendo 64 (N64)/Mario Kart 64.z64" || true
}
for PLAT in tg5040 tg5050; do
	grep -q 'CORES_PATH/m64p-server.elf' "skeleton/SYSTEM/$PLAT/paks/Emus/N64.pak/nx_n64_netplay.sh" || fail "$PLAT: relay must be called from the pak dir"
	NPELF_ROLE=host NPELF_PLAYER=1 launch $PLAT
	grep -qx -- "--game" "$TMP/wizard_args" && grep -qx "Mario Kart 64" "$TMP/wizard_args" || fail "$PLAT: wizard game"
	grep -qx -- "--max-players" "$TMP/wizard_args" || fail "$PLAT: max players"
	grep -qx -- "55445" "$TMP/server_args" || fail "$PLAT: host starts the relay"
	grep -q "NX_CORE_NETPLAY=1" "$TMP/minarch_env" || fail "$PLAT: core netplay flag"
	grep -q "NX_M64P_NETPLAY_HOST=127.0.0.1" "$TMP/minarch_env" || fail "$PLAT: host connects locally"
	grep -q "NX_M64P_NETPLAY_PLAYER=1" "$TMP/minarch_env" || fail "$PLAT: host player"
	grep -q "NETPLAY_SAVES_DIR=UNSET" "$TMP/minarch_env" || fail "$PLAT: host keeps its saves"
	grep -q "NETPLAY_ROLE=UNSET" "$TMP/minarch_env" || fail "$PLAT: no minarch lockstep"
	grep -q cleanup "$TMP/cleanup" || fail "$PLAT: wizard cleanup"
	NPELF_ROLE=client NPELF_PLAYER=2 launch $PLAT
	[ ! -f "$TMP/server_args" ] || fail "$PLAT: client must not start a relay"
	grep -q "NX_M64P_NETPLAY_HOST=10.0.0.2" "$TMP/minarch_env" || fail "$PLAT: client connects to the host"
	grep -q "NX_M64P_NETPLAY_PLAYER=2" "$TMP/minarch_env" || fail "$PLAT: client player"
	grep -q "NETPLAY_SAVES_DIR=$TMP/tmp/n64-netplay-saves" "$TMP/minarch_env" || fail "$PLAT: client saves redirected"
	NPELF_RC=1 NPELF_ROLE=host NPELF_PLAYER=1 launch $PLAT
	[ ! -f "$TMP/minarch_env" ] || fail "$PLAT: cancelled wizard must not launch the game"
done
[ "$FAIL" = 0 ] && echo "ALL PASS" || exit 1
