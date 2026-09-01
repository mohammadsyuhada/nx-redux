#!/bin/sh
# Host-side unit tests for common/. No SDL, no cross toolchain.
set -eu
cd "$(dirname "$0")"
cc -std=gnu99 -Wall -Werror -o /tmp/nx_test_paths ../paths.c test_paths.c
/tmp/nx_test_paths
cc -std=gnu99 -Wall -Werror -o /tmp/nx_test_probe ../desktop_probe.c test_desktop_probe.c && /tmp/nx_test_probe
