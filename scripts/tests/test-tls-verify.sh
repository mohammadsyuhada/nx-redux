#!/usr/bin/env bash
# Host test: every on-device download verifies TLS whenever a CA bundle is on
# the card. The vendored wget (skeleton/SYSTEM/shared/bin/wget, GNU 1.24.5) is
# built with certificate checks OFF by default, and --ca-certificate alone
# does NOT turn them on (on the Brick it accepted self-signed and wrong-host
# certificates), so each caller must pass --check-certificate=on with the
# bundle. Only a card without any bundle may fall back to
# --no-check-certificate.
set -u
cd "$(dirname "$0")/../.."
ROOT="$PWD"
FAILS=0
say()  { printf '%s\n' "$*"; }
pass() { say "PASS: $*"; }
fail() { say "FAIL: $*"; FAILS=$((FAILS+1)); }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
CA_HELPER="$ROOT/skeleton/SYSTEM/shared/bin/nx_ca_bundle.sh"

# A fake card with (or without) the system CA bundle and the shared helper.
mkcard() { # $1 = dir, $2 = with|without
  rm -rf "$1"; mkdir -p "$1/.system/shared/bin" "$1/.system/shared/ssl"
  cp "$CA_HELPER" "$1/.system/shared/bin/nx_ca_bundle.sh"
  [ "$2" = with ] && echo 'bundle' > "$1/.system/shared/ssl/ca-certificates.crt"
  return 0
}

# Runs an installer's TLS block (from the helper path to the closing fi) and
# prints the resulting NX_WGET_TLS.
tls_of() { # $1 = installer, $2 = card dir
  local block
  block="$(awk '/^_nx_ca_helper=/{p=1} p{print} p && /^fi$/{exit}' "$1")"
  [ -n "$block" ] || { echo "NO-BLOCK"; return; }
  env -i PATH="$PATH" SDCARD_PATH="$2" bash -c "set -u; $block; printf '%s' \"\$NX_WGET_TLS\""
}

for plat in tg5040 tg5050; do
  for entry in portmaster gen1recomp; do
    f="$ROOT/skeleton/SYSTEM/$plat/paks/Tools/Xtras.pak/catalog/$entry/install.sh"
    mkcard "$TMP/sd" with
    [ "$(tls_of "$f" "$TMP/sd")" = "--check-certificate=on --ca-certificate=$TMP/sd/.system/shared/ssl/ca-certificates.crt" ] \
      && pass "$plat/$entry: bundle on the card -> verified TLS" || fail "$plat/$entry: with a bundle got '$(tls_of "$f" "$TMP/sd")'"
    mkcard "$TMP/sd" without
    [ "$(tls_of "$f" "$TMP/sd")" = "--no-check-certificate" ] \
      && pass "$plat/$entry: no bundle -> legacy fallback" || fail "$plat/$entry: without a bundle got '$(tls_of "$f" "$TMP/sd")'"
    # Every wget call goes through $NX_WGET_TLS; none hard-codes the skip flag.
    bad="$(grep -n '^[^#]*wget ' "$f" | grep -v 'wget \$NX_WGET_TLS ')"
    [ -z "$bad" ] && pass "$plat/$entry: every wget call carries \$NX_WGET_TLS" || fail "$plat/$entry: wget without \$NX_WGET_TLS: $bad"
  done
  # RHH requires PortMaster, so it verifies against PortMaster's bundle and fails closed without it.
  f="$ROOT/skeleton/SYSTEM/$plat/paks/Tools/Xtras.pak/catalog/portmaster-rhh/install.sh"
  [ "$(grep -c '^[^#]*wget --check-certificate=on --ca-certificate="\$CA" ' "$f")" = 2 ] && ! grep -q '^[^#]*--no-check-certificate' "$f" \
    && pass "$plat/portmaster-rhh: both wget calls verify TLS" || fail "$plat/portmaster-rhh: unverified wget call"
done

# C callers: the shared wget helper (system updater, Cheat Database download,
# network cache) and the Cheat Database's Last-Modified probe.
grep -q 'snprintf(buf, buf_size, "--check-certificate=on --ca-certificate=' "$ROOT/workspace/all/common/wget_fetch.c" \
  && pass "wget_fetch.c: bundle -> --check-certificate=on --ca-certificate=" || fail "wget_fetch.c: bundle path doesn't force certificate checks"
[ "$(grep -c -- '--no-check-certificate' "$ROOT/workspace/all/common/wget_fetch.c")" -le 2 ] \
  && grep -q 'if (!ca) {' "$ROOT/workspace/all/common/wget_fetch.c" \
  && pass "wget_fetch.c: --no-check-certificate only on the no-bundle path" || fail "wget_fetch.c: unexpected --no-check-certificate"
grep -q 'Cheatdb_remoteLastModified(nx_ca_bundle_path(),' "$ROOT/workspace/all/cheatdb/cheatdb.c" \
  && ! grep -q 'Cheatdb_remoteLastModified([a-z]*, sizeof' "$ROOT/workspace/all/cheatdb/cheatdb.c" \
  && pass "cheatdb.c: every Last-Modified probe gets the CA bundle" || fail "cheatdb.c: a Last-Modified probe has no CA bundle"

# No other source spells the skip flag (the curl stand-in's -k mapping is
# the one deliberate exception: curl -k asks for it).
others="$(grep -rlnI -- '--no-check-certificate' "$ROOT/workspace" "$ROOT/skeleton" --include='*.c' --include='*.h' --include='*.sh' 2>/dev/null \
  | grep -v '/tests/' \
  | grep -v -e 'common/wget_fetch.c$' -e 'common/ca_bundle.h$' -e 'cheatdb/cheatdb_data.c$' \
            -e 'catalog/portmaster/install.sh$' -e 'catalog/gen1recomp/install.sh$' -e 'catalog/portmaster/pak/launch.sh$')"
[ -z "$others" ] && pass "no other source skips TLS verification" || fail "unreviewed --no-check-certificate in: $others"

[ "$FAILS" = 0 ] && say "ALL PASS" || say "$FAILS FAILURE(S)"
exit "$FAILS"
