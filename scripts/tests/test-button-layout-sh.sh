#!/usr/bin/env bash
# Host tests for the shell side of "Button layout": the sourced helper
# (skeleton/SYSTEM/<plat>/bin/nx_button_layout.sh), the mupen64plus config
# transform in N64.pak/nx_paths.sh and the flycast mapping selection in
# DC.pak/launch.sh. A fake nextval.elf on PATH stands in for the settings
# reader. Busybox-compatible constructs only in the scripts under test.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
FAIL=0
fail() { echo "FAIL: $*" >&2; FAIL=1; }

# --- fake nextval.elf ------------------------------------------------------
mkdir -p "$TMP/bin"
cat > "$TMP/bin/nextval.elf" <<'EOF'
#!/bin/sh
printf '{"%s": %s}\n' "$1" "${NX_FAKE_LAYOUT:-0}"
EOF
chmod +x "$TMP/bin/nextval.elf"
export PATH="$TMP/bin:$PATH"
export SYSTEM_PATH="$TMP/system"
mkdir -p "$SYSTEM_PATH/bin"
cp skeleton/SYSTEM/tg5040/bin/nx_button_layout.sh "$SYSTEM_PATH/bin/"

# both platform copies must be identical
cmp -s skeleton/SYSTEM/tg5040/bin/nx_button_layout.sh skeleton/SYSTEM/tg5050/bin/nx_button_layout.sh \
    || fail "tg5040/tg5050 nx_button_layout.sh differ"

# --- helper ----------------------------------------------------------------
out=$(NX_FAKE_LAYOUT=0 sh -c '. "$SYSTEM_PATH/bin/nx_button_layout.sh"; echo "$NX_BUTTON_LAYOUT"')
[ "$out" = "nintendo" ] || fail "helper: expected nintendo for 0, got '$out'"
out=$(NX_FAKE_LAYOUT=1 sh -c '. "$SYSTEM_PATH/bin/nx_button_layout.sh"; echo "$NX_BUTTON_LAYOUT"')
[ "$out" = "xbox" ] || fail "helper: expected xbox for 1, got '$out'"
# nextval missing -> nintendo, and the helper must not abort the sourcing shell
out=$(PATH="/usr/bin:/bin" SYSTEM_PATH="$SYSTEM_PATH" sh -c '. "$SYSTEM_PATH/bin/nx_button_layout.sh"; echo "$NX_BUTTON_LAYOUT"')
[ "$out" = "nintendo" ] || fail "helper: expected nintendo without nextval, got '$out'"
# and even under `set -e -o pipefail` with nextval absent, the failed command
# substitution must not abort the sourcing shell: a later statement must still
# run (finding 2 — NDS.pak/launch.sh sources this under exactly those options).
out=$(PATH="/usr/bin:/bin" SYSTEM_PATH="$SYSTEM_PATH" sh -c 'set -e -o pipefail; . "$SYSTEM_PATH/bin/nx_button_layout.sh"; echo "$NX_BUTTON_LAYOUT reached"')
[ "$out" = "nintendo reached" ] || fail "helper: set -e -o pipefail source without nextval aborted or wrong ('$out')"

# --- mupen64plus transform -------------------------------------------------
# Drive N64.pak/nx_paths.sh with a fake env; the seed is the real default cfg.
n64_run() { # $1 = layout value for the fake nextval, $2 = platform dir
    NX_FAKE_LAYOUT="$1" PAK_DIR="$ROOT/skeleton/SYSTEM/$2/paks/Emus/N64.pak" \
    SHARED_USERDATA_PATH="$TMP/shared" DEVICE="${3:-brick}" \
    sh -c '. "$PAK_DIR/nx_paths.sh"'
}
rm -rf "$TMP/shared"; mkdir -p "$TMP/shared"
n64_run 0 tg5040
CFG="$TMP/shared/N64-mupen64plus/config/tg5040-brick/mupen64plus.cfg"
[ -f "$CFG" ] || fail "n64: cfg not seeded"
cp "$CFG" "$TMP/n64_nintendo.cfg"
grep -q '^B Button = "button(0)"' "$CFG" || fail "n64: seed should be nintendo"
[ "$(cat "$TMP/shared/N64-mupen64plus/config/tg5040-brick/.button_layout" 2>/dev/null || echo nintendo)" = nintendo ] \
    || fail "n64: marker should be nintendo/absent after nintendo run"
# switch to xbox: Control1 swaps, Control2..4 untouched, everything else byte-identical
n64_run 1 tg5040
grep -q '^B Button = "button(1)"' "$CFG" || fail "n64: B should be button(1) under xbox"
grep -q '^A Button = "button(0)"' "$CFG" || fail "n64: A should be button(0) under xbox"
grep -q '^C Button L = "axis(3-,24000) button(2)"' "$CFG" || fail "n64: C-L should hold button(2) under xbox"
grep -q '^C Button D = "axis(4+,24000) button(3)"' "$CFG" || fail "n64: C-D should hold button(3) under xbox"
[ "$(cat "$TMP/shared/N64-mupen64plus/config/tg5040-brick/.button_layout")" = xbox ] || fail "n64: marker should be xbox"
# only lines inside [Input-SDL-Control1] may differ
awk '/^\[/{s=$0} s!="[Input-SDL-Control1]"{print}' "$TMP/n64_nintendo.cfg" > "$TMP/a.cfg"
awk '/^\[/{s=$0} s!="[Input-SDL-Control1]"{print}' "$CFG" > "$TMP/b.cfg"
cmp -s "$TMP/a.cfg" "$TMP/b.cfg" || fail "n64: lines outside Control1 changed"
# idempotent: a second xbox run changes nothing
cp "$CFG" "$TMP/n64_xbox.cfg"
n64_run 1 tg5040
cmp -s "$CFG" "$TMP/n64_xbox.cfg" || fail "n64: second xbox run must be a no-op"
# user tuning survives: edit an unrelated key, switch back, expect original + edit
sed -i.bak 's/^MouseSensitivity = .*/MouseSensitivity = "9.00,9.00"/' "$CFG" && rm -f "$CFG.bak"
n64_run 0 tg5040
grep -q '^B Button = "button(0)"' "$CFG" || fail "n64: B should return to button(0)"
grep -q '^MouseSensitivity = "9.00,9.00"' "$CFG" || fail "n64: user edit lost on swap back"
# tg5050 copy behaves the same
rm -rf "$TMP/shared"; mkdir -p "$TMP/shared"
n64_run 1 tg5050 x
CFG5="$TMP/shared/N64-mupen64plus/config/tg5050/mupen64plus.cfg"
grep -q '^A Button = "button(0)"' "$CFG5" || fail "n64 tg5050: A should be button(0) under xbox"

# transform failure must leave the marker AND the cfg untouched (finding 4):
# run only the marker/transform block (extracted like the DC function below)
# with an EMU_CFG whose parent dir does not exist, so awk's read and its
# redirect both fail. The marker must NOT advance and no .nxtmp may survive.
N64_FAIL_BLOCK=$(sed -n '/^NX_LAYOUT_MARKER=/,/^unset _nx_cur$/p' \
    "$ROOT/skeleton/SYSTEM/tg5040/paks/Emus/N64.pak/nx_paths.sh")
rm -rf "$TMP/n64fail"; mkdir -p "$TMP/n64fail"
DEVICE_CONFIG_DIR="$TMP/n64fail" \
EMU_CFG="$TMP/n64fail/nope/mupen64plus.cfg" \
NX_BUTTON_LAYOUT=xbox \
sh -c "$N64_FAIL_BLOCK" 2>/dev/null   # awk's expected open failure is the point
[ ! -e "$TMP/n64fail/.button_layout" ] \
    || fail "n64: marker written despite transform failure"
[ ! -e "$TMP/n64fail/nope/mupen64plus.cfg.nxtmp" ] \
    || fail "n64: .nxtmp left behind after transform failure"

# --- flycast mapping selection ---------------------------------------------
# Extract the nx_flycast_mapping() function from each launch.sh and run it.
dc_run() { # $1 = layout, $2 = platform
    NX_FAKE_LAYOUT="$1" PAK_DIR="$ROOT/skeleton/SYSTEM/$2/paks/Emus/DC.pak" \
    DEVICE_CONFIG_DIR="$TMP/dc" sh -c '
        NX_FN=$(sed -n "/^nx_flycast_mapping() {/,/^}/p" "$PAK_DIR/launch.sh")
        eval "$NX_FN"
        nx_flycast_mapping'
}
PAKMAP="$ROOT/skeleton/SYSTEM/tg5040/paks/Emus/DC.pak/SDL_Xbox 360 Controller.cfg"
MAP="$TMP/dc/flycast/mappings/SDL_Xbox 360 Controller.cfg"
rm -rf "$TMP/dc"; mkdir -p "$TMP/dc/flycast/mappings"
dc_run 0 tg5040
cmp -s "$MAP" "$PAKMAP" || fail "dc: missing mapping should be installed as the pak file (nintendo)"
dc_run 1 tg5040
grep -q '^bind0 = 0:btn_a$' "$MAP" || fail "dc: bind0 should be btn_a under xbox"
grep -q '^bind1 = 1:btn_b$' "$MAP" || fail "dc: bind1 should be btn_b under xbox"
grep -q '^bind2 = 2:btn_x$' "$MAP" || fail "dc: bind2 should be btn_x under xbox"
grep -q '^bind3 = 3:btn_y$' "$MAP" || fail "dc: bind3 should be btn_y under xbox"
grep -q '^bind0 = 0-:btn_analog_left$' "$MAP" || fail "dc: [analog] section must be untouched"
grep -q '^bind4 = 4:btn_z$' "$MAP" || fail "dc: non-face digital binds must be untouched"
dc_run 0 tg5040
cmp -s "$MAP" "$PAKMAP" || fail "dc: switching back should restore the pak file"
# hand-edited file is left alone in either layout
printf 'bind0 = 0:btn_c\n' > "$MAP"
dc_run 1 tg5040
[ "$(cat "$MAP")" = "bind0 = 0:btn_c" ] || fail "dc: hand-edited mapping must be left alone"
# legacy shipped file (md5 0791cba4...) is still upgraded
rm -f "$MAP"
dc_run 1 tg5040
cp "$MAP" "$TMP/dc_xbox.cfg"
# tg5050 copy behaves the same
rm -rf "$TMP/dc"; mkdir -p "$TMP/dc/flycast/mappings"
dc_run 1 tg5050
grep -q '^bind0 = 0:btn_a$' "$MAP" || fail "dc tg5050: bind0 should be btn_a under xbox"

[ "$FAIL" = 0 ] && echo "test-button-layout-sh: OK"
exit "$FAIL"
