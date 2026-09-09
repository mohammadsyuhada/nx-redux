#!/usr/bin/env bash
# Fixture-based test for the N64 pak's video-plugin selection plumbing.
# Exercises BOTH platform paks by sourcing each nx_paths.sh from a /bin/sh
# child with a fake environment (the same way launch.sh/options.sh do), then
# checks the seed/upgrade behaviour, the nx_video_plugin resolver, the schema
# and the default cfgs. Nothing is built or run on a device.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"

PAK_5040="$ROOT/skeleton/SYSTEM/tg5040/paks/Emus/N64.pak"
PAK_5050="$ROOT/skeleton/SYSTEM/tg5050/paks/Emus/N64.pak"
SCHEMA="$ROOT/skeleton/BASE/Emus/shared/mupen64plus/overlay_settings.json"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1" >&2; exit 1; }

md5of() { md5sum "$1" | awk '{print $1}'; }

# Source a platform's nx_paths.sh in a fresh /bin/sh with the fake environment
# a pak entry point would provide, then run $3 (a shell snippet) in that
# context and echo its output. DEVICE selects the tg5040 default cfg; tg5050's
# nx_paths.sh ignores DEVICE but the brief still passes DEVICE=tg5050.
#   $1 = platform (tg5040|tg5050)   $2 = SHARED_USERDATA_PATH   $3 = snippet
srun() {
    _p="$1"; _su="$2"; _snip="$3"
    if [ "$_p" = tg5040 ]; then _pak="$PAK_5040"; _dev=brick
    else _pak="$PAK_5050"; _dev=tg5050; fi
    SHARED_USERDATA_PATH="$_su" PAK_DIR="$_pak" DEVICE="$_dev" \
        sh -c '. "$PAK_DIR/nx_paths.sh"; '"$_snip"
}

# An "old seed": the platform default cfg with the [NxRedux] section (and its
# leading blank lines) removed, so grep in the upgrade step misses it.
strip_nxredux() {
    awk '/^\[NxRedux\]/{exit} {a[NR]=$0}
         END{n=NR; while(n>0 && a[n]=="") n--; for(i=1;i<=n;i++) print a[i]}' "$1"
}

# A minimal per-game/global cfg carrying a [NxRedux] VideoPlugin value.
mk_nx() { printf '[NxRedux]\nVideoPlugin = %s\n' "$2" > "$1"; }

strip_nxredux "$PAK_5040/default-brick.cfg" > "$TMP/oldseed-5040"
strip_nxredux "$PAK_5050/default.cfg"       > "$TMP/oldseed-5050"

# ---------------------------------------------------------------------------
# a. Fresh seed: cfg created from the default, contains [NxRedux]; the
#    resolver with no per-game file prints the platform default.
# ---------------------------------------------------------------------------
SU="$TMP/a-5040"
CFG=$(srun tg5040 "$SU" 'printf %s "$EMU_CFG"')
[ -f "$CFG" ] || fail "a/tg5040: cfg was not seeded"
grep -q '^\[NxRedux\]' "$CFG" || fail "a/tg5040: fresh seed lacks [NxRedux]"
VP=$(srun tg5040 "$SU" 'nx_video_plugin "/no/pergame.cfg" "$EMU_CFG"')
[ "$VP" = rice ] || fail "a/tg5040: default resolved to '$VP', expected rice"

SU="$TMP/a-5050"
CFG=$(srun tg5050 "$SU" 'printf %s "$EMU_CFG"')
[ -f "$CFG" ] || fail "a/tg5050: cfg was not seeded"
grep -q '^\[NxRedux\]' "$CFG" || fail "a/tg5050: fresh seed lacks [NxRedux]"
VP=$(srun tg5050 "$SU" 'nx_video_plugin "/no/pergame.cfg" "$EMU_CFG"')
[ "$VP" = gliden64 ] || fail "a/tg5050: default resolved to '$VP', expected gliden64"
echo "PASS: a fresh seed adds [NxRedux] and resolves the platform default"

# ---------------------------------------------------------------------------
# b. Pre-existing seed WITHOUT the section: the section is appended once with
#    the platform default, every pre-existing byte is unchanged, and a second
#    sourcing does not append again.
# ---------------------------------------------------------------------------
case_b() { # $1 platform  $2 expected default  $3 old-seed file
    _p="$1"; _exp="$2"; _seed="$3"; _su="$TMP/b-$_p"
    _cfg=$(srun "$_p" "$_su" 'printf %s "$EMU_CFG"')   # seeds + .initialized
    cp "$_seed" "$_cfg"                                # simulate an old seed
    _sz=$(wc -c < "$_cfg"); _m=$(md5of "$_cfg")
    srun "$_p" "$_su" ':' >/dev/null                   # upgrade appends section
    grep -q '^\[NxRedux\]' "$_cfg" || fail "b/$_p: [NxRedux] not appended"
    [ "$(grep -c '^\[NxRedux\]' "$_cfg")" -eq 1 ] || fail "b/$_p: appended more than once"
    head -c "$_sz" "$_cfg" > "$TMP/b-prefix"
    [ "$(md5of "$TMP/b-prefix")" = "$_m" ] || fail "b/$_p: pre-existing bytes changed"
    srun "$_p" "$_su" ':' >/dev/null                   # idempotent
    [ "$(grep -c '^\[NxRedux\]' "$_cfg")" -eq 1 ] || fail "b/$_p: second sourcing double-appended"
    _vp=$(srun "$_p" "$_su" 'nx_video_plugin "/no/pergame.cfg" "$EMU_CFG"')
    [ "$_vp" = "$_exp" ] || fail "b/$_p: upgraded default resolved to '$_vp', expected $_exp"
}
case_b tg5040 rice     "$TMP/oldseed-5040"
case_b tg5050 gliden64 "$TMP/oldseed-5050"
echo "PASS: b sectionless seed upgraded once, prefix intact, idempotent"

# ---------------------------------------------------------------------------
# c. Pre-existing seed WITH VideoPlugin = gliden64 on tg5040: file untouched
#    (md5), resolver prints gliden64.
# ---------------------------------------------------------------------------
SU="$TMP/c"
CFG=$(srun tg5040 "$SU" 'printf %s "$EMU_CFG"')
cp "$TMP/oldseed-5040" "$CFG"
printf '\n\n[NxRedux]\n\n# video plugin\nVideoPlugin = gliden64\n' >> "$CFG"
M=$(md5of "$CFG")
srun tg5040 "$SU" ':' >/dev/null
[ "$(md5of "$CFG")" = "$M" ] || fail "c: cfg changed though [NxRedux] already present"
VP=$(srun tg5040 "$SU" 'nx_video_plugin "/no/pergame.cfg" "$EMU_CFG"')
[ "$VP" = gliden64 ] || fail "c: resolved to '$VP', expected gliden64"
echo "PASS: c existing [NxRedux] left byte-identical, resolves its value"

# ---------------------------------------------------------------------------
# d. Per-game beats global, both directions.
# ---------------------------------------------------------------------------
mk_nx "$TMP/pg-rice.cfg"  rice
mk_nx "$TMP/gl-glide.cfg" gliden64
VP=$(srun tg5040 "$TMP/d1" "nx_video_plugin '$TMP/pg-rice.cfg' '$TMP/gl-glide.cfg'")
[ "$VP" = rice ] || fail "d: per-game rice did not beat global gliden64 ('$VP')"
mk_nx "$TMP/pg-glide.cfg" gliden64
mk_nx "$TMP/gl-rice.cfg"  rice
VP=$(srun tg5040 "$TMP/d2" "nx_video_plugin '$TMP/pg-glide.cfg' '$TMP/gl-rice.cfg'")
[ "$VP" = gliden64 ] || fail "d: per-game gliden64 did not beat global rice ('$VP')"
echo "PASS: d per-game override wins over global in both directions"

# ---------------------------------------------------------------------------
# e. Per-game cfg with only other sections -> falls through to global.
# ---------------------------------------------------------------------------
printf '[Video-GLideN64]\nUseNativeResolutionFactor = 1\n' > "$TMP/pg-other.cfg"
mk_nx "$TMP/gl-rice2.cfg" rice
VP=$(srun tg5040 "$TMP/e" "nx_video_plugin '$TMP/pg-other.cfg' '$TMP/gl-rice2.cfg'")
[ "$VP" = rice ] || fail "e: did not fall through to global ('$VP')"
echo "PASS: e per-game without [NxRedux] falls through to global"

# ---------------------------------------------------------------------------
# f. Unknown value in both per-game and global -> gliden64.
# ---------------------------------------------------------------------------
mk_nx "$TMP/pg-unk.cfg" glide64mk2
mk_nx "$TMP/gl-unk.cfg" glide64mk2
VP=$(srun tg5040 "$TMP/f" "nx_video_plugin '$TMP/pg-unk.cfg' '$TMP/gl-unk.cfg'")
[ "$VP" = gliden64 ] || fail "f: unknown value not defaulted to gliden64 ('$VP')"
echo "PASS: f unknown value falls back to gliden64"

# ---------------------------------------------------------------------------
# g. Quoted value and CRLF line endings -> rice.
# ---------------------------------------------------------------------------
printf '[NxRedux]\nVideoPlugin = "rice"\n' > "$TMP/pg-quoted.cfg"
VP=$(srun tg5040 "$TMP/g1" "nx_video_plugin '$TMP/pg-quoted.cfg' '/no/global.cfg'")
[ "$VP" = rice ] || fail "g: quoted value not parsed ('$VP')"
printf '[NxRedux]\r\nVideoPlugin = rice\r\n' > "$TMP/pg-crlf.cfg"
VP=$(srun tg5040 "$TMP/g2" "nx_video_plugin '$TMP/pg-crlf.cfg' '/no/global.cfg'")
[ "$VP" = rice ] || fail "g: CRLF line endings not tolerated ('$VP')"
printf '[NxRedux]\r\nVideoPlugin = "rice"\r\n' > "$TMP/pg-both.cfg"
VP=$(srun tg5040 "$TMP/g3" "nx_video_plugin '$TMP/pg-both.cfg' '/no/global.cfg'")
[ "$VP" = rice ] || fail "g: quoted value with CRLF not tolerated ('$VP')"
echo "PASS: g quoted and CRLF values resolve to rice"

# ---------------------------------------------------------------------------
# h. sh -n clean for launch.sh, options.sh, nx_paths.sh on both platforms.
# ---------------------------------------------------------------------------
for f in launch.sh options.sh nx_paths.sh; do
    sh -n "$PAK_5040/$f" || fail "h: sh -n failed on tg5040/$f"
    sh -n "$PAK_5050/$f" || fail "h: sh -n failed on tg5050/$f"
done
echo "PASS: h sh -n clean on both platforms"

# ---------------------------------------------------------------------------
# i. The new shared blocks are byte-identical across the two platforms.
# ---------------------------------------------------------------------------
# nx_video_plugin function body
diff <(sed -n '/^nx_video_plugin() {/,/^}/p' "$PAK_5040/nx_paths.sh") \
     <(sed -n '/^nx_video_plugin() {/,/^}/p' "$PAK_5050/nx_paths.sh") \
     >/dev/null || fail "i: nx_video_plugin differs between platforms"
# upgrade block (everything between the EMU_CFG= line and the nx_rom_base comment)
extract_upgrade() {
    awk '/^EMU_CFG=/{f=1;next} /^# Per-game override key/{f=0} f{print}' "$1"
}
diff <(extract_upgrade "$PAK_5040/nx_paths.sh") \
     <(extract_upgrade "$PAK_5050/nx_paths.sh") \
     >/dev/null || fail "i: [NxRedux] upgrade block differs between platforms"
[ -n "$(extract_upgrade "$PAK_5040/nx_paths.sh")" ] || fail "i: upgrade block empty (extraction anchors wrong)"
# launch.sh video-plugin block
extract_launch_vp() {
    awk '/^# --- video plugin/{f=1} f{print} /^# --- end video plugin/{f=0}' "$1"
}
diff <(extract_launch_vp "$PAK_5040/launch.sh") \
     <(extract_launch_vp "$PAK_5050/launch.sh") \
     >/dev/null || fail "i: launch.sh video-plugin block differs between platforms"
[ -n "$(extract_launch_vp "$PAK_5040/launch.sh")" ] || fail "i: launch.sh block empty (extraction markers wrong)"
echo "PASS: i shared blocks byte-identical across platforms"

# ---------------------------------------------------------------------------
# j. Both launch.sh use --gfx "$GFX_PLUGIN" and no longer hardcode GLideN64.
# ---------------------------------------------------------------------------
for f in "$PAK_5040/launch.sh" "$PAK_5050/launch.sh"; do
    grep -qF -- '--gfx "$GFX_PLUGIN"' "$f" || fail "j: $f missing --gfx \"\$GFX_PLUGIN\""
    if grep -qF -- '--gfx "$EMU_DIR/mupen64plus-video-GLideN64.so"' "$f"; then
        fail "j: $f still hardcodes --gfx GLideN64"
    fi
done
echo "PASS: j launch.sh passes --gfx \$GFX_PLUGIN, GLideN64 no longer hardcoded"

# ---------------------------------------------------------------------------
# k. Schema shape (python3).
# ---------------------------------------------------------------------------
python3 - "$SCHEMA" <<'PY' || fail "k: schema shape wrong (see above)"
import json, sys
d = json.load(open(sys.argv[1]))
s0 = d["sections"][0]
assert s0["name"] == "Video Plugin", s0.get("name")
assert s0["ini_section"] == "NxRedux", s0.get("ini_section")
items = s0["items"]
assert len(items) == 1, "expected exactly one item, got %d" % len(items)
it = items[0]
assert it["key"] == "VideoPlugin", it.get("key")
assert it["type"] == "enum", it.get("type")
assert it["values"] == ["gliden64", "rice"], it.get("values")
assert it["default"] == "gliden64", it.get("default")
PY
echo "PASS: k schema first section is Video Plugin / NxRedux / VideoPlugin enum"

# ---------------------------------------------------------------------------
# l. Each default cfg ends with the [NxRedux] section carrying its platform
#    default.
# ---------------------------------------------------------------------------
check_default() { # $1 cfg  $2 expected value
    tail -n 4 "$1" | grep -q '^\[NxRedux\]$' || fail "l: $1 does not end with a [NxRedux] section"
    [ "$(tail -n 1 "$1")" = "VideoPlugin = $2" ] || fail "l: $1 last line is not 'VideoPlugin = $2'"
}
check_default "$PAK_5040/default-brick.cfg"    rice
check_default "$PAK_5040/default-brickpro.cfg" rice
check_default "$PAK_5040/default-smartpro.cfg" rice
check_default "$PAK_5050/default.cfg"          gliden64
echo "PASS: l default cfgs end with [NxRedux] carrying the platform default"

echo "PASS: test-n64-video-plugin.sh"
