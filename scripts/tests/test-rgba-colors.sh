#!/usr/bin/env bash
# Host unit test for workspace/all/common/rgba.h (theme colour packing,
# 0xRRGGBBAA parsing, opacity percent mapping). Nothing is built for a device.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 \
    -I workspace/all/common \
    -o "$TMP/rgba_test" \
    scripts/tests/rgba/rgba_test.c
"$TMP/rgba_test"
