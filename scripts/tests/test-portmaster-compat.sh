#!/usr/bin/env bash
# Host test for the exFAT/FAT32 compat block the PortMaster launcher appends
# to control.txt (skeleton/SYSTEM/<plat>/paks/Tools/Xtras.pak/catalog/
# portmaster/pak/launch.sh): the `ln` fallback and the curl stand-in on
# wget. The block is cut out of
# launch.sh and sourced under bash with stub ln (refuses symlinks, like
# exFAT), mount, umount and mountpoint binaries.
set -u
cd "$(dirname "$0")/../.."
ROOT="$PWD"
FAILS=0
say()  { printf '%s\n' "$*"; }
pass() { say "PASS: $*"; }
fail() { say "FAIL: $*"; FAILS=$((FAILS+1)); }

L40="$ROOT/skeleton/SYSTEM/tg5040/paks/Tools/Xtras.pak/catalog/portmaster/pak/launch.sh"
L50="$ROOT/skeleton/SYSTEM/tg5050/paks/Tools/Xtras.pak/catalog/portmaster/pak/launch.sh"
block() { awk '/^# ---- NX Redux: exFAT\/FAT32 compat/,/^EOF$/' "$1" | sed '$d'; }

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP:?}"' EXIT
block "$L40" > "$TMP/compat.sh"
block "$L50" > "$TMP/compat50.sh"
[ -s "$TMP/compat.sh" ] && pass "compat block found" || { fail "compat block missing"; exit 1; }
cmp -s "$TMP/compat.sh" "$TMP/compat50.sh" && pass "tg5040/tg5050 compat block identical" || fail "tg5040/tg5050 compat block differ"
bash -n "$TMP/compat.sh" && pass "compat block parses" || fail "compat block syntax"

# ---- stubs --------------------------------------------------------------
BIN="$TMP/bin"; mkdir -p "$BIN"
REAL_LN="$(command -v ln)"
cat > "$BIN/ln" <<EOF
#!/usr/bin/env bash
for a in "\$@"; do case "\$a" in --symbolic|-*s*) echo "ln: Operation not permitted" >&2; exit 1 ;; esac; done
exec "$REAL_LN" "\$@"
EOF
cat > "$BIN/mount" <<EOF
#!/usr/bin/env bash
printf 'mount %s\n' "\$*" >> "$TMP/mounts.log"
[ -f "$TMP/mount_fails" ] && exit 1
exit 0
EOF
cat > "$BIN/umount" <<'EOF'
#!/usr/bin/env bash
exit 1
EOF
cat > "$BIN/mountpoint" <<'EOF'
#!/usr/bin/env bash
exit 1
EOF
# wget: logs its argv (| separated), writes a fixture to stderr (what -S
# prints), exit code from a fixture file.
cat > "$BIN/wget" <<EOF
#!/usr/bin/env bash
(IFS='|'; printf '%s\n' "\$*") >> "$TMP/wget.log"
[ -f "$TMP/wget_err" ] && cat "$TMP/wget_err" >&2
exit "\$(cat "$TMP/wget_rc" 2>/dev/null || echo 0)"
EOF
chmod +x "$BIN"/*
# Only the tools the block needs, so nothing else on the host leaks in.
for t in bash cp mkdir dirname rm cat touch sed; do
  ln -s "$(command -v "$t")" "$BIN/$t"
done

run() { # $1 = script body; runs with the stubs
  PATH="$BIN" "$BIN/bash" -c ". '$TMP/compat.sh'; $1"
}

W="$TMP/w"; G="$W/game"; H="$W/home"
fresh() { rm -rf "${TMP:?}/w"; }

# ---- ln: folder target -> bind mount -----------------------------------
fresh; mkdir -p "$G/conf" "$H/.config"; : > "$TMP/mounts.log"
run "ln -sfv '$G/conf' '$H/.config/game'"; rc=$?
[ "$rc" = 0 ] && pass "ln dir: returns 0" || fail "ln dir: rc=$rc"
[ -d "$H/.config/game" ] && pass "ln dir: mount point created" || fail "ln dir: no mount point"
grep -qx "mount --bind $G/conf $H/.config/game" "$TMP/mounts.log" && pass "ln dir: bind mount" || fail "ln dir: mounts.log='$(cat "$TMP/mounts.log")'"

# link path is an existing folder -> link goes inside it (ln semantics)
fresh; mkdir -p "$G/conf/warmux" "$H"; : > "$TMP/mounts.log"
run "ln -sfv '$G/conf/warmux' '$H/'"
grep -qx "mount --bind $G/conf/warmux $H/warmux" "$TMP/mounts.log" && pass "ln into dir: link at dir/basename" || fail "ln into dir: '$(cat "$TMP/mounts.log")'"

# -n / -T: an existing folder IS the link path
fresh; mkdir -p "$G/conf" "$H/.game"; : > "$TMP/mounts.log"
run "ln -sfn '$G/conf' '$H/.game'"
grep -qx "mount --bind $G/conf $H/.game" "$TMP/mounts.log" && pass "ln -n: link path kept" || fail "ln -n: '$(cat "$TMP/mounts.log")'"

# relative target resolves against the link's folder
fresh; mkdir -p "$G/data" "$G/sub"; : > "$TMP/mounts.log"
run "ln -s ../data '$G/sub/data'"
grep -qx "mount --bind $G/sub/../data $G/sub/data" "$TMP/mounts.log" && pass "ln relative: resolved from link dir" || fail "ln relative: '$(cat "$TMP/mounts.log")'"

# ---- ln: file target -> bind mount, copy when mount fails --------------
fresh; mkdir -p "$G" "$H"; echo cfg > "$G/game.ini"; : > "$TMP/mounts.log"
run "ln -sf '$G/game.ini' '$H/.gamerc'"
grep -qx "mount --bind $G/game.ini $H/.gamerc" "$TMP/mounts.log" && pass "ln file: bind mount" || fail "ln file: '$(cat "$TMP/mounts.log")'"
[ -f "$H/.gamerc" ] && pass "ln file: mount target file created" || fail "ln file: no target file"

fresh; mkdir -p "$G/conf" "$H"; echo cfg > "$G/game.ini"; echo s > "$G/conf/save"; touch "$TMP/mount_fails"
run "ln -sf '$G/game.ini' '$H/.gamerc'; ln -sf '$G/conf' '$H/conf'"
rm -f "$TMP/mount_fails"
[ "$(cat "$H/.gamerc" 2>/dev/null)" = cfg ] && pass "ln file: copy fallback" || fail "ln file: copy fallback missing"
[ "$(cat "$H/conf/save" 2>/dev/null)" = s ] && pass "ln dir: copy fallback" || fail "ln dir: copy fallback missing"

# ---- ln: missing target / hard link / multi-source pass through --------
fresh; mkdir -p "$H"
run "ln -s '$G/nope' '$H/x'" 2>/dev/null; rc=$?
[ "$rc" != 0 ] && pass "ln missing target: fails" || fail "ln missing target: rc=0"
echo a > "$TMP/hf"
run "ln '$TMP/hf' '$TMP/hf2'"; rc=$?
[ "$rc" = 0 ] && [ "$(cat "$TMP/hf2")" = a ] && pass "ln hard link: passes through to real ln" || fail "ln hard link: rc=$rc"

# ---- curl stand-in (PortMaster's wget underneath) -------------------------
wg() { : > "$TMP/wget.log"; rm -f "$TMP/wget_rc"; SSL_CERT_FILE="$TMP/ca.crt" run "$1"; }
touch "$TMP/ca.crt"
wg "curl https://x.test/mlox_base.txt -o mlox_base.txt"
[ "$(cat "$TMP/wget.log")" = "-q|--check-certificate=on|--ca-certificate=$TMP/ca.crt|-O|mlox_base.txt|https://x.test/mlox_base.txt" ] \
  && pass "curl -o: wget -O file, CA bundle" || fail "curl -o: '$(cat "$TMP/wget.log")'"
wg "curl -fsSL https://x.test/a.sh"
[ "$(cat "$TMP/wget.log")" = "-q|--check-certificate=on|--ca-certificate=$TMP/ca.crt|-O|-|https://x.test/a.sh" ] \
  && pass "curl -fsSL: body to stdout" || fail "curl -fsSL: '$(cat "$TMP/wget.log")'"
wg "curl -sLo out.bin https://x.test/f.bin"
grep -q '|-O|out.bin|https://x.test/f.bin$' "$TMP/wget.log" && pass "curl -sLo FILE: attached -o value" || fail "curl -sLo: '$(cat "$TMP/wget.log")'"
wg "curl -O 'https://x.test/dir/pkg.zip?token=1'"
grep -q '|-O|pkg.zip|https://x.test/dir/pkg.zip?token=1$' "$TMP/wget.log" && pass "curl -O: remote name, query dropped" || fail "curl -O: '$(cat "$TMP/wget.log")'"
wg "curl -k -H 'Accept: x' -A ua -m 30 --retry 2 -C - -o f https://x.test/f"
[ "$(cat "$TMP/wget.log")" = "-q|--check-certificate=on|--no-check-certificate|--header=Accept: x|--user-agent=ua|--timeout=30|--tries=3|-c|--ca-certificate=$TMP/ca.crt|-O|f|https://x.test/f" ] \
  && pass "curl options: -k -H -A -m --retry -C mapped" || fail "curl options: '$(cat "$TMP/wget.log")'"
: > "$TMP/wget.log"; echo 8 > "$TMP/wget_rc"
run "curl -o f https://x.test/missing"; rc=$?
[ "$rc" = 8 ] && pass "curl: wget's failure code returned" || fail "curl: rc=$rc"
rm -f "$TMP/wget_rc"
# -I (smb1r's update check: curl -sI URL | grep -i '^etag:')
printf 'Spider mode enabled. Check if remote file exists.\n  HTTP/1.1 200 OK\n  ETag: "abc"\nRemote file exists.\n' > "$TMP/wget_err"
: > "$TMP/wget.log"
out="$(SSL_CERT_FILE="$TMP/ca.crt" run "curl -sI https://x.test/game.pck")"
[ "$(cat "$TMP/wget.log")" = "--check-certificate=on|--ca-certificate=$TMP/ca.crt|-S|--spider|https://x.test/game.pck" ] \
  && pass "curl -I: wget -S --spider, no -q" || fail "curl -I: '$(cat "$TMP/wget.log")'"
[ "$out" = "$(printf 'HTTP/1.1 200 OK\nETag: "abc"')" ] \
  && pass "curl -I: only the headers, on stdout" || fail "curl -I output: '$out'"
: > "$TMP/wget.log"
out="$(run "curl --head -L https://x.test/game.pck")"
grep -q -- '|--spider|' "$TMP/wget.log" && pass "curl --head: same as -I" || fail "curl --head: '$(cat "$TMP/wget.log")'"
echo 8 > "$TMP/wget_rc"
run "curl -sI https://x.test/missing" >/dev/null; rc=$?
[ "$rc" = 8 ] && pass "curl -I: wget's failure code returned" || fail "curl -I: rc=$rc"
rm -f "$TMP/wget_rc" "$TMP/wget_err"
run "curl -s" 2>/dev/null; rc=$?
[ "$rc" != 0 ] && pass "curl: no URL fails" || fail "curl: no URL rc=0"
RC="$TMP/curlbin"; mkdir -p "$RC"; printf '#!/usr/bin/env bash\necho REAL-CURL\n' > "$RC/curl"; chmod +x "$RC/curl"
out="$(PATH="$BIN:$RC" "$BIN/bash" -c ". '$TMP/compat.sh'; curl https://x.test")"
[ "$out" = REAL-CURL ] && pass "curl: a real binary wins over the stand-in" || fail "curl: stand-in shadowed the real binary ('$out')"

say ""
[ "$FAILS" = 0 ] && say "ALL PASS" || say "$FAILS FAILURE(S)"
exit "$FAILS"
