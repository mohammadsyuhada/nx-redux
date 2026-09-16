#!/usr/bin/env bash
# Host test for the PortMaster tool-pak launcher
# (skeleton/SYSTEM/<plat>/paks/Tools/Xtras.pak/catalog/portmaster/pak/launch.sh).
# The launcher replaced portmaster.elf on 2026-09-16 and must do everything
# the elf did around a pugwash run. This drives it under sh against a fake
# card with stub python3 / show2.elf / nextval.elf binaries. The patch
# expressions are GNU-sed one-liners (busybox sed on device), so GNU sed is
# required on the host: macOS needs `brew install gnu-sed` (gsed).
set -u
cd "$(dirname "$0")/../.."
ROOT="$PWD"
FAILS=0
say()  { printf '%s\n' "$*"; }
pass() { say "PASS: $*"; }
fail() { say "FAIL: $*"; FAILS=$((FAILS+1)); }

if sed --version 2>/dev/null | grep -q 'GNU sed'; then
  GSED="$(command -v sed)"
elif command -v gsed >/dev/null 2>&1; then
  GSED="$(command -v gsed)"
else
  say "SKIP: needs GNU sed (brew install gnu-sed)"; exit 1
fi

L40="$ROOT/skeleton/SYSTEM/tg5040/paks/Tools/Xtras.pak/catalog/portmaster/pak/launch.sh"
L50="$ROOT/skeleton/SYSTEM/tg5050/paks/Tools/Xtras.pak/catalog/portmaster/pak/launch.sh"
cmp -s "$L40" "$L50" && pass "tg5040/tg5050 launch.sh identical" || fail "tg5040/tg5050 launch.sh differ"
grep -q 'portmaster.elf' "$L40" && fail "launch.sh still references portmaster.elf" || pass "launch.sh has no elf reference"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SD="$TMP/sd"
PM="$SD/Emus/shared/PortMaster"
ROMS="$SD/Roms/Ports (PORTS)"
LOGS="$SD/.userdata/tg5040/logs"
BIN="$TMP/bin"
mkdir -p "$BIN" "$PM/bin" "$PM/files" "$PM/pylibs/harbourmaster" "$PM/patchedScripts" \
         "$ROMS/.ports/apotris" "$LOGS" "$SD/.system/bin" "$SD/.userdata/shared" \
         "$SD/Tools/PortMaster.pak"
ln -sf "$GSED" "$BIN/sed"

# ---- stubs --------------------------------------------------------------
# show2.elf: record its argv, sit until killed (like the real one).
cat > "$BIN/show2.elf" <<EOF
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$TMP/show2.log"
if printf '%s' "\$*" | grep -q -- '--timeout='; then exit 0; fi
sleep 30
EOF
# nextval.elf: the button-layout query; value comes from a fixture file.
cat > "$BIN/nextval.elf" <<EOF
#!/usr/bin/env bash
printf '{"buttonlayout": %s}\n' "\$(cat "$TMP/layout" 2>/dev/null || echo 0)"
EOF
# nx_button_layout.sh: the real one from the repo.
cp "$ROOT/skeleton/SYSTEM/tg5040/bin/nx_button_layout.sh" "$SD/.system/bin/nx_button_layout.sh"
# python3: (a) disable_python_function.py -> append a marker to the target;
# (b) pugwash -> log the run, snapshot gamecontrollerdb.txt, and act on the
# one-shot simulation flags (first_run re-extract, self-update reboot).
cat > "$PM/bin/python3" <<EOF
#!/usr/bin/env bash
case "\$1" in
  *disable_python_function.py)
    printf '# portmaster_install disabled\n' >> "\$2"; exit 0 ;;
  pugwash)
    n=\$(( \$(cat "$TMP/runs" 2>/dev/null || echo 0) + 1 )); echo "\$n" > "$TMP/runs"
    cp -f "$PM/gamecontrollerdb.txt" "$TMP/db_seen_by_pugwash.\$n" 2>/dev/null
    env > "$TMP/env_seen_by_pugwash.\$n"
    if [ -f "$TMP/simulate_first_run" ]; then
      rm -f "$TMP/simulate_first_run"
      printf 'PORTS = "/mnt/SDCARD/Roms/PORTS"\nIMGS = "/mnt/SDCARD/Imgs/PORTS"\n' > "$PM/pylibs/harbourmaster/platform.py"
    fi
    if [ -f "$TMP/simulate_reboot" ]; then
      rm -f "$TMP/simulate_reboot"; touch "$PM/.pugwash-reboot"
    fi
    echo "pugwash run \$n"; exit 0 ;;
esac
exit 1
EOF
# killall: on macOS killall matches the interpreter name ("bash") for a
# script stub, so the launcher's `killall show2.elf` would miss it. Shim it
# to pkill by full path (also fine on Linux).
cat > "$BIN/killall" <<EOF
#!/usr/bin/env bash
[ "\$1" = -9 ] && shift
[ "\$1" = show2.elf ] && { pkill -9 -f "$BIN/show2.elf"; exit 0; }
exit 1
EOF
chmod +x "$BIN/show2.elf" "$BIN/nextval.elf" "$BIN/killall" "$PM/bin/python3"

# ---- runtime fixtures (upstream shapes the patches key on) ---------------
seed_runtime() {
  echo 'pugwash-gui' > "$PM/pugwash"
  printf '2026.09.08-0809\n' > "$PM/version"
  cat > "$PM/device_info.txt" <<'EOF'
if [[ $CFW_NAME == "TrimUI" ]]; then
DEVICE_NAME="TrimUI Smart Pro"
fi
case "$DEVICE_NAME" in
    "trimui smart pro"|"trimui-smart-pro")
        DEVICE_CPU="a133plus"
        ;;
esac
# GLIBC
GLIBC=2.33
EOF
  cat > "$PM/pylibs/harbourmaster/hardware.py" <<'EOF'
        ('sun50iw10', 'trimui-smart-pro'),
    "TrimUI Smart Pro": {"device": "trimui-smart-pro", "manufacturer": "TrimUI", "cfw": ["TrimUI"]},
    "TrimUI Brick": {"device": "trimui-brick", "manufacturer": "TrimUI", "cfw": ["TrimUI"]},
    "trimui-smart-pro": {"resolution": (1280, 720), "analogsticks": 2, "cpu": "a133plus", "capabilities": ["power"], "ram": 1024},
    "trimui-brick": {"resolution": (1024, 768), "analogsticks": 0, "cpu": "a133plus", "capabilities": ["power"], "ram": 1024},
    "trimui-*": "2.33",
    expand_info(info, override_resolution, override_ram)
EOF
  printf 'PORTS = "/mnt/SDCARD/Roms/PORTS"\nIMGS = "/mnt/SDCARD/Imgs/PORTS"\n' > "$PM/pylibs/harbourmaster/platform.py"
  mkdir -p "$PM/pylibs/harbourmaster/__pycache__"; touch "$PM/pylibs/harbourmaster/__pycache__/stale.pyc"
  printf 'export HOME="/mnt/SDCARD/Data/home"\n' > "$PM/mod_TrimUI.txt"
  echo 'nintendo-db' > "$PM/files/gamecontrollerdb_nintendo.txt"
  echo 'xbox-db'     > "$PM/files/gamecontrollerdb_xbox.txt"
  echo 'stale-db'    > "$PM/gamecontrollerdb.txt"
  echo 'py' > "$PM/disable_python_function.py"
  cat > "$PM/bin/busybox" <<'EOF'
#!/usr/bin/env bash
[ "$1" = "--list" ] && printf 'ls\nsh\n'
EOF
  chmod +x "$PM/bin/busybox"
  rm -f "$PM/bin/busybox_wrappers.done" "$PM/bin/ls"
  rm -rf "$PM/config"
  # ports on the card
  printf '#!/bin/bash\ncd /roms/ports/PortMaster\n' > "$ROMS/Apotris.sh"
  printf '#!/bin/bash\necho stock\n' > "$ROMS/Celeste.sh"
  printf '#!/usr/bin/env bash\necho patched\n' > "$PM/patchedScripts/Celeste.sh"
  printf '{"items": ["Apotris.sh", "apotris/"]}\n' > "$ROMS/.ports/apotris/port.json"
  echo 'cover-bytes' > "$ROMS/.ports/apotris/cover.png"
  rm -rf "$ROMS/.media"
  echo 'cache' > "$SD/.userdata/tg5040/emulist_cache.txt"
  echo 'cache' > "$SD/.userdata/tg5040/romindex_cache.txt"
  rm -rf "$SD/.userdata/shared/xtras"
  rm -f "$TMP/runs" "$TMP/show2.log" "$TMP"/db_seen_by_pugwash.* "$TMP"/env_seen_by_pugwash.*
  rm -f "$PM/.pugwash-reboot"
}

run_launcher() {
  PATH="$BIN:$PATH" \
  PLATFORM=tg5040 \
  SDCARD_PATH="$SD" \
  SYSTEM_PATH="$SD/.system" \
  SHARED_SYSTEM_PATH="$SD/.system/shared" \
  USERDATA_PATH="$SD/.userdata/tg5040" \
  SHARED_USERDATA_PATH="$SD/.userdata/shared" \
  LOGS_PATH="$LOGS" \
  sh "$L40"
}

# ---- 1. not installed -----------------------------------------------------
echo 0 > "$TMP/layout"
seed_runtime; rm -f "$PM/pugwash"
rc=0; run_launcher || rc=$?
[ "$rc" = 0 ] && pass "not installed: exits 0" || fail "not installed: exit $rc"
grep -q 'not installed' "$TMP/show2.log" 2>/dev/null \
  && pass "not installed: show2 message shown" || fail "not installed: no show2 message"
[ ! -f "$TMP/runs" ] && pass "not installed: pugwash not run" || fail "not installed: pugwash ran"

# ---- 2. normal run, nintendo layout ---------------------------------------
seed_runtime
rc=0; run_launcher || rc=$?
[ "$rc" = 0 ] && pass "run: exits 0" || fail "run: exit $rc"
[ "$(cat "$TMP/runs")" = 1 ] && pass "run: pugwash ran once" || fail "run: pugwash ran $(cat "$TMP/runs" 2>/dev/null) times"
grep -q 'Launching PortMaster' "$TMP/show2.log" && pass "run: launching splash shown" || fail "run: no launching splash"
pgrep -f "$BIN/show2.elf" >/dev/null && fail "run: show2 still running after launcher exit" || pass "run: show2 not left running"
[ "$(cat "$TMP/db_seen_by_pugwash.1")" = 'xbox-db' ] && pass "run: pugwash saw the xbox db" || fail "run: pugwash saw '$(cat "$TMP/db_seen_by_pugwash.1" 2>/dev/null)'"
[ "$(cat "$PM/gamecontrollerdb.txt")" = 'nintendo-db' ] && pass "run: user layout (nintendo) restored" || fail "run: db after run is '$(cat "$PM/gamecontrollerdb.txt")'"
grep -q "^HOME=$SD/.userdata/shared/PORTS-portmaster$" "$TMP/env_seen_by_pugwash.1" && pass "run: HOME pointed at PORTS-portmaster" || fail "run: HOME wrong"
grep -q "^HM_PORTS_DIR=$ROMS/.ports$" "$TMP/env_seen_by_pugwash.1" && pass "run: HM_PORTS_DIR set" || fail "run: HM_PORTS_DIR wrong"
grep -q "^SSL_CERT_FILE=$PM/ssl/certs/ca-certificates.crt$" "$TMP/env_seen_by_pugwash.1" && pass "run: SSL_CERT_FILE set" || fail "run: SSL_CERT_FILE wrong"
[ -f "$LOGS/portmaster.txt" ] && pass "run: launcher log written" || fail "run: launcher log missing"
grep -q 'pugwash run 1' "$LOGS/portmaster_pugwash.txt" 2>/dev/null && pass "run: pugwash log tee'd" || fail "run: pugwash log missing"
grep -q '"disclaimer": true' "$PM/config/config.json" 2>/dev/null && pass "run: default config written" || fail "run: default config missing"
# patches
grep -q 'sun55iw3' "$PM/device_info.txt" && pass "patch: device_info Smart Pro S detect" || fail "patch: device_info Smart Pro S detect missing"
grep -q '"trimui smart pro s"|"trimui-smart-pro-s")' "$PM/device_info.txt" && pass "patch: device_info Smart Pro S case" || fail "patch: device_info Smart Pro S case missing"
grep -q 'DEVICE_NAME="TrimUI Brick Pro"; ANALOG_STICKS=2' "$PM/device_info.txt" && pass "patch: device_info Brick Pro" || fail "patch: device_info Brick Pro missing"
[ "$(grep -c 'Smart Pro S' "$PM/device_info.txt")" -ge 1 ] && [ "$(grep -c 'TrimUI Brick Pro' "$PM/device_info.txt")" = 1 ] && pass "patch: device_info applied once (idempotent guard)" || fail "patch: device_info duplicated"
grep -q "('sun55iw3',  'trimui-smart-pro-s')," "$PM/pylibs/harbourmaster/hardware.py" && pass "patch: hardware.py sun55iw3" || fail "patch: hardware.py sun55iw3 missing"
grep -q '"TrimUI Smart Pro S": {"device": "trimui-smart-pro-s"' "$PM/pylibs/harbourmaster/hardware.py" && pass "patch: hardware.py SPS nice name" || fail "patch: hardware.py SPS nice name missing"
grep -q '"trimui-smart-pro-s": {"resolution": (1280, 720)' "$PM/pylibs/harbourmaster/hardware.py" && pass "patch: hardware.py SPS device" || fail "patch: hardware.py SPS device missing"
grep -q '"trimui-smart-pro-s\*": "2.33"' "$PM/pylibs/harbourmaster/hardware.py" && pass "patch: hardware.py SPS glibc" || fail "patch: hardware.py SPS glibc missing"
grep -q '"TrimUI Brick Pro": {"device": "trimui-brick-pro"' "$PM/pylibs/harbourmaster/hardware.py" && pass "patch: hardware.py Brick Pro nice name" || fail "patch: hardware.py Brick Pro nice name missing"
grep -q '"trimui-brick-pro": {"resolution": (1024, 768), "analogsticks": 2' "$PM/pylibs/harbourmaster/hardware.py" && pass "patch: hardware.py Brick Pro device" || fail "patch: hardware.py Brick Pro device missing"
grep -q 'Path("/mnt/SDCARD/tg5040-brickpro").exists(): info\["device"\] = "trimui-brick-pro"' "$PM/pylibs/harbourmaster/hardware.py" && pass "patch: hardware.py Brick Pro override" || fail "patch: hardware.py Brick Pro override missing"
[ ! -e "$PM/pylibs/harbourmaster/__pycache__/stale.pyc" ] && pass "patch: pycache cleared" || fail "patch: pycache kept"
grep -q "\"$ROMS\"" "$PM/pylibs/harbourmaster/platform.py" && pass "patch: platform.py PORTS path" || fail "patch: platform.py PORTS path missing"
grep -q "\"$ROMS/.media\"" "$PM/pylibs/harbourmaster/platform.py" && pass "patch: platform.py IMGS path" || fail "patch: platform.py IMGS path missing"
grep -q 'portmaster_install disabled' "$PM/pylibs/harbourmaster/platform.py" && pass "patch: portmaster_install disabled" || fail "patch: portmaster_install not disabled"
grep -q "$SD/.userdata/shared/PORTS-portmaster" "$PM/mod_TrimUI.txt" && pass "patch: mod_TrimUI HOME" || fail "patch: mod_TrimUI HOME missing"
grep -q "^export controlfolder=\"$PM\"$" "$PM/control.txt" && pass "patch: control.txt controlfolder" || fail "patch: control.txt controlfolder wrong"
grep -q 'PM_PORTNAME="${PM_SCRIPTNAME%.sh}"' "$PM/control.txt" && pass "patch: control.txt PM_PORTNAME" || fail "patch: control.txt PM_PORTNAME wrong"
grep -q 'export GPTOKEYB2="$ESUDO env LD_PRELOAD=$controlfolder/libinterpose.aarch64.so $controlfolder/gptokeyb2 $ESUDOKILL"' "$PM/control.txt" && pass "patch: control.txt GPTOKEYB2" || fail "patch: control.txt GPTOKEYB2 wrong"
# post-run sync
grep -q "^cd $PM$" "$ROMS/Apotris.sh" && pass "post: port script path fixed" || fail "post: port script path not fixed"
head -1 "$ROMS/Apotris.sh" | grep -q '^#!/usr/bin/env bash$' && pass "post: port script shebang fixed" || fail "post: shebang not fixed"
grep -q 'echo patched' "$ROMS/Celeste.sh" && pass "post: patchedScripts applied" || fail "post: patchedScripts not applied"
[ "$(cat "$ROMS/.media/Apotris.png" 2>/dev/null)" = 'cover-bytes' ] && pass "post: cover art synced to .media" || fail "post: cover art missing"
[ ! -e "$SD/.userdata/tg5040/emulist_cache.txt" ] && [ ! -e "$SD/.userdata/tg5040/romindex_cache.txt" ] && pass "post: launcher caches invalidated" || fail "post: launcher caches survived"
[ "$(cat "$SD/.userdata/shared/xtras/portmaster.version" 2>/dev/null)" = '2026.09.08-0809' ] && pass "post: xtras version marker synced" || fail "post: xtras marker '$(cat "$SD/.userdata/shared/xtras/portmaster.version" 2>/dev/null)'"
[ -x "$PM/bin/ls" ] && grep -q 'busybox ls' "$PM/bin/ls" && [ ! -e "$PM/bin/sh" ] && [ -f "$PM/bin/busybox_wrappers.done" ] && pass "post: busybox wrappers recreated" || fail "post: busybox wrappers missing"

# ---- 3. second run is idempotent (patches not duplicated) ----------------
rc=0; run_launcher || rc=$?
[ "$(grep -c '"trimui smart pro s"|"trimui-smart-pro-s")' "$PM/device_info.txt")" = 1 ] && pass "idempotent: device_info case once" || fail "idempotent: device_info case duplicated"
[ "$(grep -c '"trimui-brick-pro": {' "$PM/pylibs/harbourmaster/hardware.py")" = 1 ] && pass "idempotent: hardware.py Brick Pro once" || fail "idempotent: hardware.py Brick Pro duplicated"
[ "$(grep -c '"trimui-smart-pro-s": {' "$PM/pylibs/harbourmaster/hardware.py")" = 1 ] && pass "idempotent: hardware.py SPS once" || fail "idempotent: hardware.py SPS duplicated"

# ---- 4. xbox layout restored after pugwash --------------------------------
echo 1 > "$TMP/layout"
seed_runtime
run_launcher >/dev/null 2>&1
[ "$(cat "$PM/gamecontrollerdb.txt")" = 'xbox-db' ] && pass "xbox: user layout restored" || fail "xbox: db after run is '$(cat "$PM/gamecontrollerdb.txt")'"
echo 0 > "$TMP/layout"

# ---- 5. retry when pugwash re-extracted unpatched pylibs -----------------
seed_runtime; touch "$TMP/simulate_first_run"
run_launcher >/dev/null 2>&1
[ "$(cat "$TMP/runs")" = 2 ] && pass "retry: pugwash re-run once after fresh pylibs" || fail "retry: ran $(cat "$TMP/runs") times"
grep -q "\"$ROMS\"" "$PM/pylibs/harbourmaster/platform.py" && pass "retry: platform.py re-patched" || fail "retry: platform.py left unpatched"

# ---- 6. self-update reboot marker loops -----------------------------------
seed_runtime; touch "$TMP/simulate_reboot"
run_launcher >/dev/null 2>&1
[ "$(cat "$TMP/runs")" = 2 ] && pass "reboot marker: pugwash restarted" || fail "reboot marker: ran $(cat "$TMP/runs") times"
[ ! -e "$PM/.pugwash-reboot" ] && pass "reboot marker: consumed" || fail "reboot marker: left behind"

pkill -f "$BIN/show2.elf" >/dev/null 2>&1
[ "$FAILS" = 0 ] && say "test-portmaster-launch: OK" || say "test-portmaster-launch: $FAILS FAILURE(S)"
exit "$FAILS"
