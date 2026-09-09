#!/usr/bin/env bash
# Host unit test for minarch's turbo shaper (ma_turbo.c): compiles the module
# with the host compiler against a model of the TrimUI input daemon's pulse
# train and checks the shaped cadence. Nothing is built or run on a device.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 \
    -I workspace/all/minarch \
    -o "$TMP/turbo_shaper_test" \
    workspace/all/minarch/ma_turbo.c scripts/tests/turbo/turbo_shaper_test.c
"$TMP/turbo_shaper_test"
