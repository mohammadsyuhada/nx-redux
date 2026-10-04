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
# tg5040 carries Brick-only patches (Brick Pro sticks, pugwash UI scale);
# otherwise the two must match, so tg5050 may add nothing of its own.
[ -z "$(diff "$L40" "$L50" | grep '^>' | grep -v 'mod_TrimUI.txt (HOME)$')" ] \
  && pass "tg5050 launch.sh is tg5040 minus the Brick-only patches" || fail "tg5040/tg5050 launch.sh diverge beyond the Brick-only patches"
grep -q 'NX Redux: Brick Pro sticks' "$L50" || grep -q 'NX Redux: UI scale' "$L50" \
  && fail "tg5050 launch.sh carries a Brick-only patch" || pass "Brick-only patches stay out of tg5050"
grep -q 'portmaster.elf' "$L40" && fail "launch.sh still references portmaster.elf" || pass "launch.sh has no elf reference"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SD="$TMP/sd"
PM="$SD/Emus/shared/PortMaster"
ROMS="$SD/Roms/Ports (PORTS)"
LOGS="$SD/.userdata/tg5040/logs"
BIN="$TMP/bin"
mkdir -p "$BIN" "$PM/bin" "$PM/files" "$PM/pylibs/harbourmaster" \
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
  # The 2026.09.19+ probe, cut down to the lines the NX Redux patches anchor on.
  cat > "$PM/device_info.txt" <<'EOF'
export CFW_NAME="Unknown"
if [ "$CFW_NAME" = "Unknown" ] && { [ -f "/etc/os-release" ] || [ -f "/usr/lib/os-release" ]; }; then
    export CFW_NAME="os-release"
fi
if [ "$CFW_VERSION" = "Unknown" ] && { [ -f "/etc/os-release" ] || [ -f "/usr/lib/os-release" ]; }; then
    export CFW_VERSION="os-release"
fi
detect_dynamic_controls() {
    ANALOG_STICKS=0
    export ANALOG_STICKS
    export ANALOG_TRIGGERS
}
CAPS=()
export DEVICE_CAPABILITIES="$(echo "${CAPS[@]}" | tr ' ' '\n' | sort -u | tr '\n' ' ' | sed 's/[ \t]*$//')"
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
  mkdir -p "$PM/patchedScripts"; printf '#!/usr/bin/env bash\necho patched\n' > "$PM/patchedScripts/Celeste.sh"
  printf '{"items": ["Apotris.sh", "apotris/"]}\n' > "$ROMS/.ports/apotris/port.json"
  echo 'cover-bytes' > "$ROMS/.ports/apotris/cover.png"
  # a third-party port with no art of its own: PortMaster's cached screenshot
  mkdir -p "$ROMS/.ports/nxgame" "$PM/config/images_nextos" "$PM/lib"
  printf '{"items": ["NX Game.sh", "nxgame/"], "name": "nxgame.zip"}\n' > "$ROMS/.ports/nxgame/port.json"
  echo 'shot-bytes' > "$PM/config/images_nextos/NXGame.screenshot.jpg"
  echo 'gles1' > "$PM/lib/libGLESv1_CM.so.1"; rm -f "$PM/lib/libGLESv1_CM.so"
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
  sh "$L40" "$@"
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
grep -q 'echo stock' "$ROMS/Celeste.sh" && [ ! -e "$PM/patchedScripts" ] && pass "post: patchedScripts retired, port script left alone" || fail "post: patchedScripts still applied or kept"
[ "$(cat "$ROMS/.media/Apotris.png" 2>/dev/null)" = 'cover-bytes' ] && pass "post: cover art synced to .media" || fail "post: cover art missing"
[ "$(cat "$ROMS/.media/NX Game.png" 2>/dev/null)" = 'shot-bytes' ] && pass "post: no port art -> PortMaster's cached screenshot" || fail "post: catalog screenshot fallback missing"
[ "$(cat "$PM/lib/libGLESv1_CM.so" 2>/dev/null)" = 'gles1' ] && pass "post: unversioned libGLESv1_CM.so made (tg5040)" || fail "post: libGLESv1_CM.so missing"
[ ! -e "$SD/.userdata/tg5040/emulist_cache.txt" ] && [ ! -e "$SD/.userdata/tg5040/romindex_cache.txt" ] && pass "post: launcher caches invalidated" || fail "post: launcher caches survived"
# extras.elf owns the version record (catalog version, #108); the launcher must not touch it.
[ ! -e "$SD/.userdata/shared/xtras/portmaster.version" ] && pass "post: xtras version marker left to extras.elf" || fail "post: launcher wrote xtras marker '$(cat "$SD/.userdata/shared/xtras/portmaster.version" 2>/dev/null)'"
[ -x "$PM/bin/ls" ] && grep -q 'busybox ls' "$PM/bin/ls" && [ ! -e "$PM/bin/sh" ] && [ -f "$PM/bin/busybox_wrappers.done" ] && pass "post: busybox wrappers recreated" || fail "post: busybox wrappers missing"

# ---- 3. second run is idempotent (patches not duplicated) ----------------
rc=0; run_launcher || rc=$?
[ "$(grep -c '^ln() {' "$PM/control.txt")" = 1 ] && [ "$(grep -c 'NX Redux: UI scale' "$PM/pugwash")" -le 1 ] && pass "idempotent: control.txt and pugwash patched once" || fail "idempotent: patches duplicated"

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

# ---- 7. --patch-only (ports_launch.sh after an install / self-update) -----
# PortMaster's install and self-update put back the stock control.txt; the Ports launcher runs the tool's patch-only
# mode before a port when control.txt isn't the patched one.
seed_runtime
printf 'controlfolder=/roms/ports/PortMaster\nsource /roms/ports/PortMaster/device_info.txt\n' > "$PM/control.txt"
rm -f "$LOGS/portmaster.txt"
out="$(run_launcher --patch-only 2>&1)"; rc=$?
[ "$rc" = 0 ] && pass "patch-only: exits 0" || fail "patch-only: exit $rc"
grep -q 'Patched for NxRedux' "$PM/control.txt" && grep -q "^export controlfolder=\"$PM\"$" "$PM/control.txt" \
  && pass "patch-only: control.txt patched" || fail "patch-only: control.txt left stock"
grep -q 'NX Redux: exFAT/FAT32 compat' "$PM/control.txt" && pass "patch-only: control.txt carries the ln fallback" || fail "patch-only: ln fallback missing"
grep -q 'portmaster_install disabled' "$PM/pylibs/harbourmaster/platform.py" \
  && pass "patch-only: the other patches applied" || fail "patch-only: other patches missing"
[ ! -e "$PM/patchedScripts" ] && pass "patch-only: patchedScripts retired" || fail "patch-only: patchedScripts kept"
printf '%s' "$out" | grep -q 'not found' && fail "patch-only: called an undefined function: $(printf '%s' "$out" | grep 'not found')" \
  || pass "patch-only: every patch step defined before use"
[ ! -f "$TMP/runs" ] && pass "patch-only: pugwash not run" || fail "patch-only: pugwash ran"
[ ! -f "$TMP/show2.log" ] && pass "patch-only: no splash" || fail "patch-only: splash shown"
[ ! -f "$LOGS/portmaster.txt" ] && printf '%s' "$out" | grep -q 'NxRedux patches applied' \
  && pass "patch-only: output to the caller, not portmaster.txt" || fail "patch-only: log redirected"
seed_runtime; rm -f "$PM/pugwash"
rc=0; run_launcher --patch-only >/dev/null 2>&1 || rc=$?
[ "$rc" = 0 ] && [ ! -f "$TMP/show2.log" ] && pass "patch-only: not installed exits quietly" || fail "patch-only: not installed rc $rc"
for P in "$ROOT"/skeleton/SYSTEM/tg50?0/paks/Tools/Xtras.pak/catalog/portmaster/pak/ports_launch.sh; do
  grep -q "grep -q 'NX Redux: exFAT/FAT32 compat' \"\$EMU_DIR/control.txt\"" "$P" && grep -q 'sh "$PM_TOOL" --patch-only' "$P" \
    && grep -q "grep -q 'NX Redux: TrimUI capability' \"\$DI\"" "$P" \
    && grep -q "grep -q -- '--patch-only' \"\$PM_TOOL\"" "$P" && ! grep -q 'patch_device_info' "$P" \
    && pass "ports_launch: patches an unpatched control.txt ($(printf '%s' "$P" | grep -o 'tg50[45]0'))" \
    || fail "ports_launch: no patch guard in $P"
done

# ---- 8. device_info.txt patches (2026.09.19+ probe) -------------------------
# They only act on TrimUI (/usr/trimui), so run them here as functions cut out
# of launch.sh with that guard dropped.
DIT="$TMP/di"; mkdir -p "$DIT"
for plat in tg5040 tg5050; do
  L="$ROOT/skeleton/SYSTEM/$plat/paks/Tools/Xtras.pak/catalog/portmaster/pak/launch.sh"
  awk '/^patch_device_info_(trimui|brickpro_sticks|trimui_cap)\(\) \{/,/^}$/' "$L" \
    | sed 's#\[ -d /usr/trimui \] || return 0#:#' > "$DIT/fns.$plat.sh"
  cp "$PM/device_info.txt" "$DIT/device_info.txt"
  echo cached > "$DIT/device_info_trimui_trimui_brick.env"
  echo cached > "$DIT/device_info_unknown_unknown.env"
  (cd "$DIT" && . "./fns.$plat.sh" && for i in 1 2; do
     patch_device_info_trimui "$DIT/device_info.txt"
     type patch_device_info_brickpro_sticks >/dev/null 2>&1 && patch_device_info_brickpro_sticks "$DIT/device_info.txt"
     patch_device_info_trimui_cap "$DIT/device_info.txt"
   done)
  D="$DIT/device_info.txt"
  grep -q 'smartpros) export DEVICE_NAME="TrimUI Smart Pro S"' "$D" && pass "di $plat: TrimUI model" || fail "di $plat: TrimUI model missing"
  grep -B1 '^export DEVICE_CAPABILITIES=' "$D" | grep -q 'CAPS+=("trimui")' && pass "di $plat: trimui capability" || fail "di $plat: trimui capability missing"
  [ "$(grep -c 'NX Redux: TrimUI capability' "$D")" = 1 ] && [ "$(grep -c 'NX Redux: TrimUI firmware' "$D")" = 1 ] \
    && pass "di $plat: applied once" || fail "di $plat: duplicated"
  [ ! -e "$DIT/device_info_trimui_trimui_brick.env" ] && [ ! -e "$DIT/device_info_unknown_unknown.env" ] \
    && pass "di $plat: stale probe caches dropped" || fail "di $plat: stale probe cache kept"
  if [ "$plat" = tg5040 ]; then
    grep -q 'NX Redux: Brick Pro sticks' "$D" && pass "di $plat: Brick Pro sticks" || fail "di $plat: Brick Pro sticks missing"
  fi
  grep -c 'hardware.py' "$L" | grep -qx 0 && pass "launch.sh $plat: no hardware.py patches left" || fail "launch.sh $plat: hardware.py still patched"
done

# ---- 9. ports_launch.sh calls --patch-only only when something is unpatched ---
# Runs the real ports_launch.sh with no ROM (it stops right after the guard)
# against a stub tool that records each call.
G="$TMP/guard"
guard_run() { # $1 = platform; prints how many times the tool ran
  rm -f "$G/tool.calls"
  SDCARD_PATH="$G/sd" USERDATA_PATH="$G/sd/.userdata/$1" SHARED_USERDATA_PATH="$G/sd/.userdata/shared" \
  SHARED_SYSTEM_PATH="$G/sd/.system/shared" LOGS_PATH="$G/logs" \
    sh "$G/sd/Emus/PORTS.pak/launch.sh" >/dev/null 2>&1
  [ -f "$G/tool.calls" ] && wc -l < "$G/tool.calls" | tr -d ' ' || echo 0
}
for plat in tg5040 tg5050; do
  rm -rf "${G:?}"; mkdir -p "$G/sd/Emus/PORTS.pak" "$G/sd/Emus/shared/PortMaster" "$G/sd/Tools/PortMaster.pak" "$G/logs"
  cp "$ROOT/skeleton/SYSTEM/$plat/paks/Tools/Xtras.pak/catalog/portmaster/pak/ports_launch.sh" "$G/sd/Emus/PORTS.pak/launch.sh"
  printf '#!/bin/sh\n# supports --patch-only\necho "$*" >> "%s"\n' "$G/tool.calls" > "$G/sd/Tools/PortMaster.pak/launch.sh"
  GPM="$G/sd/Emus/shared/PortMaster"
  printf '# ---- NX Redux: exFAT/FAT32 compat for port scripts ----\n' > "$GPM/control.txt"
  printf '# NX Redux: TrimUI capability\nexport DEVICE_CAPABILITIES="x"\n' > "$GPM/device_info.txt"
  mkdir -p "$G/sd/.userdata/$plat"; echo 'pcm.nx_game {}' > "$G/sd/.userdata/$plat/.asoundrc"
  [ "$(guard_run $plat)" = 0 ] && pass "guard $plat: all patched -> tool not run" || fail "guard $plat: ran with everything patched"
  cmp -s "$G/sd/.userdata/$plat/.asoundrc" "$G/sd/.userdata/shared/PORTS-portmaster/.asoundrc" \
    && pass "ports_launch $plat: audiomon's .asoundrc copied into the port HOME" || fail "ports_launch $plat: .asoundrc not copied"
  grep -q "can't stat\|No such file" "$G/logs/PORTS.txt" && fail "ports_launch $plat: errors in the log: $(grep "can't stat\|No such file" "$G/logs/PORTS.txt" | head -2)" \
    || pass "ports_launch $plat: clean log up to the guard"
  printf 'controlfolder=/roms/ports/PortMaster\n' > "$GPM/control.txt"
  [ "$(guard_run $plat)" = 1 ] && pass "guard $plat: stock control.txt -> --patch-only" || fail "guard $plat: stock control.txt not repaired"
  printf '# ---- NX Redux: exFAT/FAT32 compat for port scripts ----\n' > "$GPM/control.txt"
  printf 'export DEVICE_CAPABILITIES="x"\n' > "$GPM/device_info.txt"
  [ "$(guard_run $plat)" = 1 ] && pass "guard $plat: stock device_info.txt -> --patch-only" || fail "guard $plat: stock device_info.txt not repaired"
  printf 'GLIBC=2.33\n' > "$GPM/device_info.txt"
  [ "$(guard_run $plat)" = 0 ] && pass "guard $plat: device_info.txt without the probe anchor left alone" || fail "guard $plat: ran on an unpatchable device_info.txt"
  printf 'controlfolder=/roms/ports/PortMaster\n' > "$GPM/control.txt"
  printf '#!/bin/sh\necho "$*" >> "%s"\n' "$G/tool.calls" > "$G/sd/Tools/PortMaster.pak/launch.sh"
  [ "$(guard_run $plat)" = 0 ] && pass "guard $plat: tool without --patch-only not run" || fail "guard $plat: old tool would open the GUI"
done

# ---- 10. ports_launch.sh: /roms/ports/<dir> rewrite and ALSA config ------
# main() needs root (mounts, sysfs), so the rewrite runs as extracted: the
# same sed expressions on a third-party wrapper's lines.
for plat in tg5040 tg5050; do
  P="$ROOT/skeleton/SYSTEM/$plat/paks/Tools/Xtras.pak/catalog/portmaster/pak/ports_launch.sh"
  printf '#!/bin/bash\ncontrolfolder="/roms/ports/PortMaster"\n[ -f "/roms/ports/castle/run.sh" ] && RUN="/roms/ports/castle/run.sh"\n' > "$TMP/wrap.sh"
  PATH="$BIN:$PATH" EMU_DIR=/E TEMP_DATA_DIR=/T bash -c "$(grep -A3 '^    sed -i -e "s|/roms/ports/PortMaster|' "$P" | sed 's/"\$ROM_PATH"$/"$1"/')" _ "$TMP/wrap.sh"
  grep -q '^controlfolder="/E"$' "$TMP/wrap.sh" && grep -q '"/T/ports/castle/run.sh" ] && RUN="/T/ports/castle/run.sh"$' "$TMP/wrap.sh" \
    && pass "ports_launch $plat: /roms/ports/<dir> -> the .ports bind mount, PortMaster path first" || fail "ports_launch $plat: rewrite wrong: $(tr '\n' ' ' < "$TMP/wrap.sh")"
  # Audio routing: bound over /etc/asound.conf only once cleanup is trapped, and unbound by cleanup.
  awk '/trap "cleanup"/ { t = NR } /^    bind_audio_routing$/ { b = NR } END { exit !(t && b > t) }' "$P" \
    && grep -q 'cat "$NX_ASOUND_DEST" "$HOME/.asoundrc" > "$NX_ASOUND"' "$P" \
    && awk '/^cleanup\(\)/,/^}/' "$P" | grep -q 'umount "$NX_ASOUND_DEST"' \
    && pass "ports_launch $plat: .asoundrc bound over /etc/asound.conf for the session, unbound by cleanup" \
    || fail "ports_launch $plat: audio routing bind missing or not cleaned up"
done

pkill -f "$BIN/show2.elf" >/dev/null 2>&1
[ "$FAILS" = 0 ] && say "test-portmaster-launch: OK" || say "test-portmaster-launch: $FAILS FAILURE(S)"
exit "$FAILS"
