#!/bin/sh
cd "$(dirname "$0")"
./settings.elf > "$LOGS_PATH/settings.txt" 2>&1
