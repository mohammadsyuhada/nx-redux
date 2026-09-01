#!/bin/sh
cd "$(dirname "$0")"
./options.elf > "$LOGS_PATH/options.txt" 2>&1
