#!/bin/sh
cd "$(dirname "$0")"
./scraper.elf > "$LOGS_PATH/scraper.txt" 2>&1
