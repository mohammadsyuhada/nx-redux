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
CLEANUP="$TMP/wizard_cleanup"
OUT="$TMP/out"

# Stub wizard: records argv; parses --fetch-to; optionally writes a session file
# (role = $NPELF_ROLE, peer caps = $NPELF_PEER_CAPS) and, for a client fetch,
# drops the host's save into --fetch-to under the fixed staged name. Exits
# NPELF_RC. --cleanup is logged to $NPELF_CLEANUP_OUT and removes the session.
cat > "$TMP/bin/netplay.elf" <<'EOF'
#!/bin/sh
if [ "$1" = "--cleanup" ]; then
	printf '%s\n' "$*" >> "$NPELF_CLEANUP_OUT"
	rm -f "$NPELF_SESSION_OUT"
	exit 0
fi
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
NETPLAY_PEER_CAPS='${NPELF_PEER_CAPS:-}'
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

run() { # $1 = rom path; env: NPELF_RC NPELF_WRITE_SESSION NPELF_ROLE NPELF_FETCH NPELF_PEER_CAPS EMU_EXE EMU_TAG
	rm -f "$ARGS" "$OUT" "$CLEANUP"
	rm -rf "$TMP/stage" "$TMP/data"
	PATH="$TMP/bin:$PATH" LOGS_PATH="$TMP/logs" \
	NETPLAY_LAUNCH_FLAG="$FLAG" NETPLAY_SESSION_FILE="$SESSION" \
	NETPLAY_SAVE_STAGE_DIR="$TMP/stage" NETPLAY_SAVE_DATA_DIR="$TMP/data" \
	SAVES_PATH="$TMP/Saves" EMU_TAG="${EMU_TAG:-SFC}" EMU_EXE="${EMU_EXE:-snes9x}" \
	NPELF_ARGS_OUT="$ARGS" NPELF_SESSION_OUT="$SESSION" NPELF_CLEANUP_OUT="$CLEANUP" \
	NPELF_PEER_CAPS="${NPELF_PEER_CAPS:-}" \
	NPELF_RC="${NPELF_RC:-0}" NPELF_WRITE_SESSION="${NPELF_WRITE_SESSION:-}" \
	NPELF_ROLE="${NPELF_ROLE:-host}" NPELF_FETCH="${NPELF_FETCH:-}" \
	sh "$TMP/launch.sh" "$1" > "$OUT"
	# `VAR=x run …` prefixes outlive the call when this runs as `sh` (POSIX
	# mode keeps assignments made before a function call), so case 3's
	# NPELF_RC=1 would fail every later wizard. Clear them; no-op under bash.
	unset NPELF_RC NPELF_WRITE_SESSION NPELF_ROLE NPELF_FETCH NPELF_PEER_CAPS EMU_EXE EMU_TAG
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

# 9. mGBA: USB Cable only, no save sync, and it says which core it runs.
touch "$FLAG"; rm -f "$SESSION"
EMU_EXE=mgba EMU_TAG=MGBA NPELF_WRITE_SESSION=1 NPELF_ROLE=host run "/Roms/GBA/Pokemon Ruby.gba"
grep -q -- '--modes usb' "$ARGS" || fail "mgba: wizard not limited to USB: $(cat "$ARGS")"
grep -q -- '--serve-dir' "$ARGS" && fail "mgba: link core should not sync saves: $(cat "$ARGS")" || true
grep -q -- '--caps core=mgba,tag=MGBA' "$ARGS" || fail "mgba: core caps missing: $(cat "$ARGS")"
grep -q '^MINARCH role=host .* saves=$' "$OUT" || fail "mgba: emulator not reached: $(cat "$OUT")"

# 10. gpSP passes its core too, with every mode on offer.
touch "$FLAG"; rm -f "$SESSION"
EMU_EXE=gpsp EMU_TAG=GBA NPELF_WRITE_SESSION=1 run "/Roms/GBA/Pokemon Ruby.gba"
grep -q -- '--caps core=gpsp,tag=GBA' "$ARGS" || fail "gpsp: core caps missing: $(cat "$ARGS")"
grep -q -- '--modes' "$ARGS" && fail "gpsp: modes should not be limited: $(cat "$ARGS")" || true
grep -q 'MINARCH' "$OUT" || fail "gpsp: emulator not reached: $(cat "$OUT")"

# 11. Every lockstep core says which core and folder it runs, too.
touch "$FLAG"; rm -f "$SESSION"
EMU_EXE=picodrive EMU_TAG=MD NPELF_WRITE_SESSION=1 run "/Roms/MD/Sonic.md"
grep -q -- '--caps core=picodrive,tag=MD$' "$ARGS" || fail "picodrive: core caps missing: $(cat "$ARGS")"
touch "$FLAG"; rm -f "$SESSION"
EMU_EXE=genesis_plus_gx EMU_TAG=GPGX NPELF_WRITE_SESSION=1 run "/Roms/GPGX/Sonic.md"
grep -q -- '--caps core=genesis_plus_gx,tag=GPGX$' "$ARGS" || fail "gpgx: core caps missing: $(cat "$ARGS")"
touch "$FLAG"; rm -f "$SESSION"
NPELF_WRITE_SESSION=1 run "/Roms/SFC/Mario Kart.sfc"
grep -q -- '--caps core=snes9x,tag=SFC$' "$ARGS" || fail "snes9x: core caps missing: $(cat "$ARGS")"

# 12. The wizard refuses a different core in its handshake; a session it did
#     write is never second-guessed here, whatever the peer's caps say.
touch "$FLAG"; rm -f "$SESSION"
EMU_EXE=mgba EMU_TAG=MGBA NPELF_WRITE_SESSION=1 NPELF_ROLE=client NPELF_PEER_CAPS=core=gpsp,tag=GBA run "/Roms/GBA/Pokemon Ruby.gba"
grep -q '^MINARCH role=client ' "$OUT" || fail "script refused a wizard-approved session: $(cat "$OUT")"
[ ! -f "$CLEANUP" ] || fail "script ran cleanup on a wizard-approved session"

# 13. A tag (or core) outside the caps charset skips the check rather than
#     failing the wizard's --caps validation.
touch "$FLAG"; rm -f "$SESSION"
EMU_EXE=picodrive EMU_TAG="My MD" NPELF_WRITE_SESSION=1 run "/Roms/My MD/Sonic.md"
grep -q -- '--caps' "$ARGS" && fail "unsafe tag passed caps: $(cat "$ARGS")" || true
grep -q 'MINARCH role=host' "$OUT" || fail "unsafe tag: emulator not reached: $(cat "$OUT")"
touch "$FLAG"; rm -f "$SESSION"
EMU_EXE='pico;drive' EMU_TAG=MD NPELF_WRITE_SESSION=1 run "/Roms/MD/Sonic.md"
grep -q -- '--caps' "$ARGS" && fail "unsafe core passed caps: $(cat "$ARGS")" || true

echo "PASS: netplay-prelaunch.sh"
