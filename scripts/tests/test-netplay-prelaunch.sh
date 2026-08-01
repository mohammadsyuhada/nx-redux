#!/usr/bin/env bash
# Fixture-based test for the shared minarch netplay pre-launch helper.
# Runs the helper exactly as a pak launch.sh would: sourced from a /bin/sh
# script, with netplay.elf stubbed on PATH and the /tmp paths overridden.
set -euo pipefail
cd "$(dirname "$0")/../.."
HELPER="$PWD/skeleton/SYSTEM/shared/bin/netplay-prelaunch.sh"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1" >&2; exit 1; }

mkdir -p "$TMP/bin" "$TMP/logs"
FLAG="$TMP/netplay_launch"
SESSION="$TMP/netplay_session"
ARGS="$TMP/wizard_args"
OUT="$TMP/out"

# Stub wizard: records argv; optionally writes a session file; exits NPELF_RC.
cat > "$TMP/bin/netplay.elf" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" > "$NPELF_ARGS_OUT"
if [ -n "$NPELF_WRITE_SESSION" ]; then
	cat > "$NPELF_SESSION_OUT" <<'SESH'
NETPLAY_ROLE=host
NETPLAY_PEER_IP=10.0.0.2
NETPLAY_MODE=hotspot
NETPLAY_GAME='stub'
NETPLAY_PREV_SSID=''
SESH
fi
exit "${NPELF_RC:-0}"
EOF
chmod +x "$TMP/bin/netplay.elf"

# Simulated pak launch.sh: source the helper, then a stand-in minarch line
# that prints whether the session env arrived.
cat > "$TMP/launch.sh" <<EOF
#!/bin/sh
ROM="\$1"
. "$HELPER"
sh -c 'echo "MINARCH role=\${NETPLAY_ROLE:-} peer=\${NETPLAY_PEER_IP:-} mode=\${NETPLAY_MODE:-}"'
EOF
chmod +x "$TMP/launch.sh"

run() { # $1 = rom path; NPELF_RC / NPELF_WRITE_SESSION control the stub
	rm -f "$ARGS" "$OUT"
	PATH="$TMP/bin:$PATH" LOGS_PATH="$TMP/logs" \
	NETPLAY_LAUNCH_FLAG="$FLAG" NETPLAY_SESSION_FILE="$SESSION" \
	NPELF_ARGS_OUT="$ARGS" NPELF_SESSION_OUT="$SESSION" \
	NPELF_RC="${NPELF_RC:-0}" NPELF_WRITE_SESSION="${NPELF_WRITE_SESSION:-}" \
	sh "$TMP/launch.sh" "$1" > "$OUT"
}

# 1. Plain launch: no flag -> wizard never invoked, minarch line reached, no env.
rm -f "$FLAG" "$SESSION"
run "/Roms/SFC/Mario Kart.sfc"
[ ! -f "$ARGS" ] || fail "wizard invoked without launch flag"
grep -q '^MINARCH role= peer= mode=$' "$OUT" || fail "plain launch didn't reach minarch cleanly"

# 2. Success path: flag consumed, correct --game, session env exported.
rm -f "$SESSION"; touch "$FLAG"
NPELF_WRITE_SESSION=1 run "/Roms/SFC/Mario Kart.sfc"
[ ! -f "$FLAG" ] || fail "launch flag not consumed"
grep -q -- '--game Mario Kart --session-file' "$ARGS" || fail "wizard args wrong: $(cat "$ARGS")"
grep -q '^MINARCH role=host peer=10.0.0.2 mode=hotspot$' "$OUT" || fail "session env not exported"

# 3. Wizard cancelled (exit 1): launch.sh exits 0 BEFORE the minarch line.
touch "$FLAG"; rm -f "$SESSION"
NPELF_RC=1 run "/Roms/SFC/Mario Kart.sfc"   # set -e would abort if exit != 0
grep -q 'MINARCH' "$OUT" && fail "cancelled wizard fell through to the emulator" || true

# 4. Wizard exit 0 but session file missing (defensive): same bail-out.
touch "$FLAG"; rm -f "$SESSION"
run "/Roms/SFC/Mario Kart.sfc"              # stub does NOT write a session
grep -q 'MINARCH' "$OUT" && fail "missing session file fell through" || true

# 5. Dotted game name: only the final extension is stripped.
touch "$FLAG"; rm -f "$SESSION"
NPELF_WRITE_SESSION=1 run "/Roms/FC/Super Mario Bros. 3.nes"
grep -q -- '--game Super Mario Bros. 3 --session-file' "$ARGS" || fail "dotted name mishandled: $(cat "$ARGS")"

echo "PASS: netplay-prelaunch.sh"
