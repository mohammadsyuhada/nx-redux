#!/bin/sh
cd "$(dirname "$0")"
./sync.elf > "$LOGS_PATH/sync.txt" 2>&1
