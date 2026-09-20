#!/usr/bin/env bash
# Host unit test for the launcher CPU frequency policy state machine
# (workspace/all/nextui/cpu_policy.c): boot phase -> menu cap -> idle cap.
# Pure C, no SDL or platform headers needed.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -I workspace/all/nextui \
    -o "$TMP/cpu_policy_test" workspace/all/nextui/cpu_policy.c \
    scripts/tests/cpupolicy/cpu_policy_test.c
"$TMP/cpu_policy_test"
