#!/bin/sh
# Host test for the nx_update_logs_path helper in MinUI.pak/launch.sh:
# debugLogging=1 -> LOGS_PATH is the persistent .userdata logs dir;
# debugLogging=0 (or missing key) -> LOGS_PATH is a freshly wiped tmpfs dir.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../../../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Fake nextval.elf: echoes whatever the test wants for the debugLogging key.
mkdir -p "$tmp/bin"
cat > "$tmp/bin/nextval.elf" <<'FAKE'
#!/bin/sh
[ "$1" = "debugLogging" ] && { printf '{"debugLogging": %s}\n' "$FAKE_DEBUG_LOGGING"; exit 0; }
echo '{}'
FAKE
chmod +x "$tmp/bin/nextval.elf"
export PATH="$tmp/bin:$PATH"
export USERDATA_PATH="$tmp/userdata"

fail() { echo "FAIL: $*" >&2; exit 1; }

for plat in tg5040 tg5050 desktop; do
	launcher="$root/skeleton/SYSTEM/$plat/paks/MinUI.pak/launch.sh"
	sh -n "$launcher" || fail "$plat launch.sh does not parse"
	# Extract just the helper function so the test never runs the real boot script.
	sed -n '/^nx_update_logs_path() {/,/^}/p' "$launcher" > "$tmp/helper.sh"
	[ -s "$tmp/helper.sh" ] || fail "$plat: helper not found"
	# Redirect the tmpfs target under $tmp so the host /tmp is untouched.
	sed 's|/tmp/nx-logs|'"$tmp"'/nx-logs|g' "$tmp/helper.sh" > "$tmp/helper_t.sh"

	# 1. setting on -> persistent path, created, flag exported, stale tmpfs dir removed
	mkdir -p "$tmp/nx-logs"; echo stale > "$tmp/nx-logs/old.txt"
	out=$(FAKE_DEBUG_LOGGING=1 sh -c '. "'"$tmp"'/helper_t.sh"; nx_update_logs_path; echo "$NX_DEBUG_LOGGING $LOGS_PATH"')
	[ "$out" = "1 $USERDATA_PATH/logs" ] || fail "$plat on: got '$out'"
	[ -d "$USERDATA_PATH/logs" ] || fail "$plat on: logs dir not created"
	[ ! -e "$tmp/nx-logs" ] || fail "$plat on: stale tmpfs dir not removed"

	# 2. setting off -> tmpfs path, pre-existing file wiped
	mkdir -p "$tmp/nx-logs"; echo stale > "$tmp/nx-logs/old.txt"
	out=$(FAKE_DEBUG_LOGGING=0 sh -c '. "'"$tmp"'/helper_t.sh"; nx_update_logs_path; echo "$NX_DEBUG_LOGGING $LOGS_PATH"')
	[ "$out" = "0 $tmp/nx-logs" ] || fail "$plat off: got '$out'"
	[ -d "$tmp/nx-logs" ] || fail "$plat off: tmpfs dir not created"
	[ ! -e "$tmp/nx-logs/old.txt" ] || fail "$plat off: stale log not wiped"

	# 3. key absent (old settings file) -> behaves as off
	out=$(FAKE_DEBUG_LOGGING= sh -c '. "'"$tmp"'/helper_t.sh"; nx_update_logs_path; echo "$LOGS_PATH"')
	[ "$out" = "$tmp/nx-logs" ] || fail "$plat missing key: got '$out'"
	rm -rf "$USERDATA_PATH" "$tmp/nx-logs"
done
echo "test_launcher_logs_path: OK"
