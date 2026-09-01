#!/bin/sh
cd "$(dirname "$0")"
./extras.elf > "$LOGS_PATH/extras.txt" 2>&1
