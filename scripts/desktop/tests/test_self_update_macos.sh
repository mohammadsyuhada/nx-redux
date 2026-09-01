#!/bin/sh
# macOS swap-backend test for self-update.sh. Runs on the host (no docker):
# builds a fake "old" .app and a fake "new" release zip, serves the zip via
# python3's http.server, and exercises the success path plus every guard /
# failure point the script defines. Each failure case asserts the bundle
# under test is byte-for-byte untouched (still prints its original marker).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SELFUPDATE="$HERE/../self-update.sh"
PORT=8902
BASE="http://127.0.0.1:$PORT"

TMP="$(mktemp -d)"
SRV_PID=""
cleanup() { kill "$SRV_PID" 2>/dev/null || true; rm -rf "$TMP"; }
trap cleanup EXIT

STAGE="$TMP/stage"     # per-case "installed" bundles (NXREDUX_BUNDLE targets)
WEB="$TMP/web"         # served over http
BIN_OPEN="$TMP/bin-open"           # stubs `open` only
BIN_OPEN_MVFAIL="$TMP/bin-mvfail"  # stubs `open` + fails the swap `mv`
mkdir -p "$STAGE" "$WEB" "$BIN_OPEN" "$BIN_OPEN_MVFAIL"

fails=0
ok() { echo "ok - $1"; }
bad() { echo "FAIL - $1"; fails=$((fails + 1)); }

# --- stub commands -------------------------------------------------------
cat > "$BIN_OPEN/open" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$BIN_OPEN/open"
cp "$BIN_OPEN/open" "$BIN_OPEN_MVFAIL/open"

# Real mv for everything except the "unpacked/NXRedux.app -> $BUNDLE" swap
# move (its source path always contains "/unpacked/"); that one call is
# faked to fail so the script's restore-on-swap-failure branch runs for
# real, without ever touching the on-disk bundle content ourselves.
cat > "$BIN_OPEN_MVFAIL/mv" <<'EOF'
#!/bin/sh
case "$1" in
	*/unpacked/*) exit 1 ;;
	*) exec /bin/mv "$@" ;;
esac
EOF
chmod +x "$BIN_OPEN_MVFAIL/mv"

# --- fixture helpers -------------------------------------------------------
make_app() { # make_app <app-dir> <marker>
	mkdir -p "$1/Contents/MacOS"
	printf '#!/bin/sh\necho "%s"\n' "$2" > "$1/Contents/MacOS/NXRedux"
	chmod +x "$1/Contents/MacOS/NXRedux"
}
app_marker() { "$1/Contents/MacOS/NXRedux"; } # prints the running app's marker

# --- web fixtures ----------------------------------------------------------
make_app "$TMP/newsrc/NXRedux.app" new
ditto -c -k --keepParent "$TMP/newsrc/NXRedux.app" "$WEB/new.zip"

printf 'not a zip file, just bytes' > "$WEB/corrupt.zip"

mkdir -p "$TMP/badsrc/NXRedux.app/Contents/MacOS" # no NXRedux binary inside
ditto -c -k --keepParent "$TMP/badsrc/NXRedux.app" "$WEB/missing-app.zip"

python3 -m http.server "$PORT" --directory "$WEB" >"$TMP/http.log" 2>&1 &
SRV_PID=$!
for i in $(seq 1 20); do
	curl -sf -o /dev/null "$BASE/new.zip" && break
	sleep 0.25
done

# --- test: successful swap --------------------------------------------------
OLD="$STAGE/success/NXRedux.app"
make_app "$OLD" old
if PATH="$BIN_OPEN:$PATH" NXREDUX_BUNDLE="$OLD" "$SELFUPDATE" "$BASE/new.zip" >"$TMP/out.log" 2>&1; then
	AFTER="$(app_marker "$OLD")"
	if [ "$AFTER" = "new" ]; then
		ok "successful swap replaces bundle content"
	else
		bad "successful swap: bundle prints '$AFTER', expected 'new'"
	fi
	if find "$STAGE/success" -maxdepth 1 -name '.nxredux-update.*' | grep -q .; then
		bad "successful swap: leftover .nxredux-update.* tmp dir not cleaned up"
	else
		ok "successful swap cleans up its tmp dir"
	fi
else
	bad "successful swap: self-update.sh exited non-zero: $(cat "$TMP/out.log")"
fi

# --- generic failure-case runner -------------------------------------------
# Asserts: non-zero exit, stderr contains $4, the bundle's marker is
# unchanged from before the call (proving no partial/failed swap survives),
# and no .nxredux-update.* tmp dir (partial-download hygiene) is left behind
# next to the bundle.
run_fail_case() { # run_fail_case <label> <bundle-dir> <url> <expect-in-stderr> [path-dir]
	label="$1"; bundle="$2"; url="$3"; expect="$4"; pathdir="${5:-$BIN_OPEN}"
	before="$(app_marker "$bundle")"
	set +e
	err="$(PATH="$pathdir:$PATH" NXREDUX_BUNDLE="$bundle" "$SELFUPDATE" "$url" 2>&1 1>/dev/null)"
	rc=$?
	set -e
	if [ "$rc" -eq 0 ]; then
		bad "$label: expected non-zero exit, got 0"
		return
	fi
	case "$err" in
		*"$expect"*) : ;;
		*) bad "$label: stderr missing '$expect' (got: $err)"; return ;;
	esac
	after="$(app_marker "$bundle")"
	if [ "$after" != "$before" ]; then
		bad "$label: bundle content changed ($before -> $after)"
		return
	fi
	if find "$(dirname "$bundle")" -maxdepth 1 -name '.nxredux-update.*' | grep -q .; then
		bad "$label: leftover .nxredux-update.* tmp dir (partial download not cleaned up)"
		return
	fi
	ok "$label"
}

DL="$STAGE/download-fail/NXRedux.app"; make_app "$DL" old
run_fail_case "download failed (404) leaves bundle untouched" \
	"$DL" "$BASE/does-not-exist.zip" "download failed"

CORRUPT="$STAGE/corrupt/NXRedux.app"; make_app "$CORRUPT" old
run_fail_case "corrupt archive leaves bundle untouched" \
	"$CORRUPT" "$BASE/corrupt.zip" "archive corrupt"

MISSING="$STAGE/missing-app/NXRedux.app"; make_app "$MISSING" old
run_fail_case "archive missing app leaves bundle untouched" \
	"$MISSING" "$BASE/missing-app.zip" "archive missing app"

SWAPFAIL="$STAGE/swap-fail/NXRedux.app"; make_app "$SWAPFAIL" old
run_fail_case "swap failure restores previous bundle" \
	"$SWAPFAIL" "$BASE/new.zip" "swap failed; restored previous" "$BIN_OPEN_MVFAIL"

TRANSLOC="$STAGE/AppTranslocation/fake/NXRedux.app"; make_app "$TRANSLOC" old-transloc
run_fail_case "translocated bundle refused, untouched" \
	"$TRANSLOC" "$BASE/new.zip" "translocated"

# --- test: missing NXREDUX_BUNDLE ------------------------------------------
set +e
err="$(PATH="$BIN_OPEN:$PATH" env -u NXREDUX_BUNDLE "$SELFUPDATE" "$BASE/new.zip" 2>&1 1>/dev/null)"
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
	ok "missing NXREDUX_BUNDLE refused (rc=$rc)"
else
	bad "missing NXREDUX_BUNDLE: expected non-zero exit"
fi

if [ "$fails" -eq 0 ]; then
	echo "test_self_update_macos: OK"
else
	echo "test_self_update_macos: $fails assertion(s) FAILED"
	exit 1
fi
