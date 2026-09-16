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

# Stub wizard: records argv; parses --fetch-to; optionally writes a session file
# (role = $NPELF_ROLE) and, for a client fetch, drops the host's save into
# --fetch-to under the fixed staged name. Exits NPELF_RC.
cat > "$TMP/bin/netplay.elf" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" > "$NPELF_ARGS_OUT"
fetchto=""; prev=""
for a in "$@"; do
	[ "$prev" = "--fetch-to" ] && fetchto="$a"
	prev="$a"
done
if [ -n "$NPELF_WRITE_SESSION" ]; then
	cat > "$NPELF_SESSION_OUT" <<SESH
NETPLAY_ROLE=${NPELF_ROLE:-host}
NETPLAY_PEER_IP=10.0.0.2
NETPLAY_MODE=hotspot
NETPLAY_GAME='stub'
NETPLAY_PREV_SSID=''
SESH
fi
if [ -n "${NPELF_FETCH:-}" ] && [ -n "$fetchto" ]; then
	mkdir -p "$fetchto"
	printf 'HOSTSAVE' > "$fetchto/nxsave.srm"
fi
exit "${NPELF_RC:-0}"
EOF
chmod +x "$TMP/bin/netplay.elf"

# Simulated pak launch.sh: source the helper, then a stand-in minarch line that
# prints whether the session env (and the client save redirect) arrived.
cat > "$TMP/launch.sh" <<EOF
#!/bin/sh
ROM="\$1"
. "$HELPER"
sh -c 'echo "MINARCH role=\${NETPLAY_ROLE:-} peer=\${NETPLAY_PEER_IP:-} mode=\${NETPLAY_MODE:-} saves=\${NETPLAY_SAVES_DIR:-}"'
EOF
chmod +x "$TMP/launch.sh"

run() { # $1 = rom path; env: NPELF_RC NPELF_WRITE_SESSION NPELF_ROLE NPELF_FETCH EMU_EXE
	rm -f "$ARGS" "$OUT"
	rm -rf "$TMP/stage" "$TMP/data"
	PATH="$TMP/bin:$PATH" LOGS_PATH="$TMP/logs" \
	NETPLAY_LAUNCH_FLAG="$FLAG" NETPLAY_SESSION_FILE="$SESSION" \
	NETPLAY_SAVE_STAGE_DIR="$TMP/stage" NETPLAY_SAVE_DATA_DIR="$TMP/data" \
	SAVES_PATH="$TMP/Saves" EMU_TAG="SFC" EMU_EXE="${EMU_EXE:-snes9x}" \
	NPELF_ARGS_OUT="$ARGS" NPELF_SESSION_OUT="$SESSION" \
	NPELF_RC="${NPELF_RC:-0}" NPELF_WRITE_SESSION="${NPELF_WRITE_SESSION:-}" \
	NPELF_ROLE="${NPELF_ROLE:-host}" NPELF_FETCH="${NPELF_FETCH:-}" \
	sh "$TMP/launch.sh" "$1" > "$OUT"
}

# 1. Plain launch: no flag -> wizard never invoked, minarch reached, no env.
rm -f "$FLAG" "$SESSION"
run "/Roms/SFC/Mario Kart.sfc"
[ ! -f "$ARGS" ] || fail "wizard invoked without launch flag"
grep -q '^MINARCH role= peer= mode= saves=$' "$OUT" || fail "plain launch didn't reach minarch cleanly: $(cat "$OUT")"

# 2. Success path: flag consumed, correct --game, session env exported.
rm -f "$SESSION"; touch "$FLAG"
NPELF_WRITE_SESSION=1 run "/Roms/SFC/Mario Kart.sfc"
[ ! -f "$FLAG" ] || fail "launch flag not consumed"
grep -q -- '--game Mario Kart --session-file' "$ARGS" || fail "wizard args wrong: $(cat "$ARGS")"
grep -q '^MINARCH role=host peer=10.0.0.2 mode=hotspot saves=$' "$OUT" || fail "session env not exported: $(cat "$OUT")"

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

# 6. Lockstep HOST: save-sync args passed, real save staged under the safe name,
#    minarch NOT redirected, real save untouched.
mkdir -p "$TMP/Saves/SFC"; printf 'MYSAVE' > "$TMP/Saves/SFC/Mario Kart.srm"
touch "$FLAG"; rm -f "$SESSION"
NPELF_WRITE_SESSION=1 NPELF_ROLE=host run "/Roms/SFC/Mario Kart.sfc"
grep -q -- '--serve-dir .* --fetch-to .* --fetch-files nxsave.srm,nxsave.sav' "$ARGS" \
	|| fail "host: sync args missing: $(cat "$ARGS")"
[ -f "$TMP/stage/nxsave.srm" ] || fail "host: real save not staged under safe name"
grep -q 'MYSAVE' "$TMP/stage/nxsave.srm" || fail "host: staged save has wrong content"
grep -q '^MINARCH role=host .* saves=$' "$OUT" || fail "host: should NOT redirect saves: $(cat "$OUT")"
grep -q 'MYSAVE' "$TMP/Saves/SFC/Mario Kart.srm" || fail "host: real save was altered"

# 7. Lockstep CLIENT: host save fetched, renamed for minarch, save dir
#    redirected, and the player's real save left untouched.
printf 'MYSAVE' > "$TMP/Saves/SFC/Mario Kart.srm"
touch "$FLAG"; rm -f "$SESSION"
NPELF_WRITE_SESSION=1 NPELF_ROLE=client NPELF_FETCH=1 run "/Roms/SFC/Mario Kart.sfc"
grep -q "^MINARCH role=client .* saves=$TMP/data$" "$OUT" || fail "client: saves not redirected: $(cat "$OUT")"
[ -f "$TMP/data/Mario Kart.srm" ] || fail "client: fetched save not renamed for minarch"
grep -q 'HOSTSAVE' "$TMP/data/Mario Kart.srm" || fail "client: isolated save has wrong content"
grep -q 'MYSAVE' "$TMP/Saves/SFC/Mario Kart.srm" || fail "client: real save was overwritten"
[ ! -f "$TMP/Saves/SFC/nxsave.srm" ] || fail "client: staged name leaked into real Saves"

# 8. Link core (gambatte): NO save sync -- link-cable play needs distinct saves.
touch "$FLAG"; rm -f "$SESSION"
EMU_EXE=gambatte NPELF_WRITE_SESSION=1 NPELF_ROLE=host run "/Roms/GB/Tetris.gb"
grep -q -- '--serve-dir' "$ARGS" && fail "link core should not sync saves: $(cat "$ARGS")" || true
grep -q '^MINARCH role=host .* saves=$' "$OUT" || fail "link core: unexpected save redirect: $(cat "$OUT")"

echo "PASS: netplay-prelaunch.sh"
