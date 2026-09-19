#!/usr/bin/env bash
# Host test for the tg5050 speaker anti-pop guard.
#
# 1. Compiles and runs the pure-logic state-machine unit test
#    (workspace/all/audiomon/spk_guard.c + tests/test_spk_guard.c) with the host
#    compiler — nothing is built or run on a device.
# 2. Guards the integration invariants the guard relies on:
#    - the real amp gate is /sys/class/speaker/mute, owned solely by audiomon:
#      libmsettings SetRawVolume must NOT write that node any more, and
#      PLAT_overrideMute must be a no-op (no class/speaker write) — otherwise
#      either would race the guard.
#    - libmsettings' setMixerDefaults keeps forcing "SPK Switch" on (harmless:
#      the mute node overrides it).
#    - audiomon initialises the guard BEFORE the boot priming stream in
#      write_default_audio_file(), so that stream powers the codec amp-off.
set -euo pipefail
cd "$(dirname "$0")/../.."

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# 1. Unit test (build line mirrors tests/test_spk_guard.c's header).
( cd workspace/all/audiomon && cc -I. spk_guard.c tests/test_spk_guard.c -o "$TMP/test_spk_guard" )
"$TMP/test_spk_guard"

# 2. Integration invariants (string checks, like test-audiomon-bt-disconnect.sh).
python3 - <<'PY'
from pathlib import Path

msettings = Path("workspace/tg5050/libmsettings/msettings.c").read_text()
assert '{"SPK Switch", 1}' in msettings, \
    'setMixerDefaults must keep forcing "SPK Switch" on (mute node overrides it)'
assert 'class/speaker/mute' not in msettings, \
    'SetRawVolume must not write the speaker mute node — audiomon owns it'

platform = Path("workspace/tg5050/platform/platform.c").read_text()
start = platform.index("void PLAT_overrideMute")
body = platform[start:platform.index("\n}\n", start) + 2]
assert 'class/speaker' not in body, \
    'PLAT_overrideMute must be a no-op (no speaker mute write) on tg5050'

audiomon = Path("workspace/all/audiomon/audiomon.c").read_text()
main_body = audiomon[audiomon.index("int main(int argc"):]
# Match the call statements (";"), not the same names in comments.
assert main_body.index("spk_guard_setup();") < main_body.index("write_default_audio_file();"), \
    "audiomon must set up the guard before the boot priming stream"

print("audiomon speaker anti-pop guard checks passed")
PY
