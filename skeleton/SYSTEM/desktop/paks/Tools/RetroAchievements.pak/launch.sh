#!/bin/sh
cd "$(dirname "$0")"
./ratools.elf > "$LOGS_PATH/ratools.txt" 2>&1
