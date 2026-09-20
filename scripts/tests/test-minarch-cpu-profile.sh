#!/usr/bin/env bash
# Host unit test for minarch's per-pak CPU range (ma_cpu_profile.c): the
# minarch_cpu_min/max MHz keys -> validated kHz range. Pure C, no deps.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -I workspace/all/minarch \
    -o "$TMP/cpu_profile_test" workspace/all/minarch/ma_cpu_profile.c \
    scripts/tests/minarch_cpu_profile/cpu_profile_test.c
"$TMP/cpu_profile_test"
