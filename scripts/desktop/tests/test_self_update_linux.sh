#!/bin/sh
# Linux swap-backend test for self-update.sh. Container-only (needs curl +
# python3 from the `appimage` compose service):
#   docker compose run --rm appimage scripts/desktop/tests/test_self_update_linux.sh
# Fakes $APPIMAGE as a plain executable file, serves replacements over
# python3's http.server, and exercises the success path plus the Linux
# guards (download failure, non-AppImage payload). Every failure case
# asserts the target file is byte-for-byte untouched.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SELFUPDATE="$HERE/../self-update.sh"
PORT=8903
BASE="http://127.0.0.1:$PORT"

WORK="$(mktemp -d)"
SRV_PID=""
cleanup() { kill "$SRV_PID" 2>/dev/null || true; rm -rf "$WORK"; }
trap cleanup EXIT

STAGE="$WORK/stage" # per-case "installed" AppImage files ($APPIMAGE targets)
WEB="$WORK/web"     # served over http
mkdir -p "$STAGE" "$WEB"

fails=0
ok() { echo "ok - $1"; }
bad() { echo "FAIL - $1"; fails=$((fails + 1)); }

make_appimage() { # make_appimage <path> <marker>
	printf '#!/bin/sh\necho "%s"\n' "$2" > "$1"
	chmod +x "$1"
}

# --- web fixtures -----------------------------------------------------
make_appimage "$WEB/new.AppImage" new
printf 'not an appimage, just plain bytes, no shebang here' > "$WEB/notanappimage.AppImage"

python3 -m http.server "$PORT" --directory "$WEB" >"$WORK/http.log" 2>&1 &
SRV_PID=$!
for i in $(seq 1 20); do
	curl -sf -o /dev/null "$BASE/new.AppImage" && break
	sleep 0.25
done

# --- test: successful swap ----------------------------------------------
TARGET="$STAGE/NXRedux-success.AppImage"
make_appimage "$TARGET" old
export APPIMAGE="$TARGET"
if "$SELFUPDATE" "$BASE/new.AppImage" >"$WORK/out.log" 2>&1; then
	# the relaunch `(sleep 1; "$TARGET") &` is harmless here: $TARGET is now
	# our own fake script, so letting it run is itself the "stub" -- give it
	# a moment then reap it rather than leaving an orphan in the container.
	sleep 2
	AFTER="$("$TARGET")"
	if [ "$AFTER" = "new" ]; then
		ok "successful swap replaces AppImage content"
	else
		bad "successful swap: target prints '$AFTER', expected 'new'"
	fi
	if [ -x "$TARGET" ]; then
		ok "successful swap leaves target executable"
	else
		bad "successful swap: target lost its executable bit"
	fi
	if [ -e "$TARGET.part" ]; then
		bad "successful swap: leftover .part file not cleaned up"
	else
		ok "successful swap leaves no .part hygiene leftover"
	fi
else
	bad "successful swap: self-update.sh exited non-zero: $(cat "$WORK/out.log")"
fi
unset APPIMAGE

# --- generic failure-case runner ------------------------------------------
run_fail_case() { # run_fail_case <label> <target-file> <url> <expect-in-stderr>
	label="$1"; target="$2"; url="$3"; expect="$4"
	before="$(cat "$target")"
	set +e
	err="$(APPIMAGE="$target" "$SELFUPDATE" "$url" 2>&1 1>/dev/null)"
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
	after="$(cat "$target")"
	if [ "$after" != "$before" ]; then
		bad "$label: target content changed"
		return
	fi
	if [ -e "$target.part" ]; then
		bad "$label: leftover .part file not cleaned up"
		return
	fi
	ok "$label"
}

DL="$STAGE/NXRedux-download-fail.AppImage"
printf '#!/bin/sh\necho old\n' > "$DL"; chmod +x "$DL"
run_fail_case "download failed (404) leaves target untouched" \
	"$DL" "$BASE/does-not-exist.AppImage" "download failed"

BADPAYLOAD="$STAGE/NXRedux-bad-payload.AppImage"
printf '#!/bin/sh\necho old\n' > "$BADPAYLOAD"; chmod +x "$BADPAYLOAD"
run_fail_case "non-AppImage payload leaves target untouched" \
	"$BADPAYLOAD" "$BASE/notanappimage.AppImage" "not an AppImage"

if [ "$fails" -eq 0 ]; then
	echo "test_self_update_linux: OK"
else
	echo "test_self_update_linux: $fails assertion(s) FAILED"
	exit 1
fi
