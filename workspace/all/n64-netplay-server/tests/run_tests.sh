#!/bin/sh
# test_client spawns the server itself, from ./m64p-server-native
cd "$(dirname "$0")/.." && exec ./tests/test_client
