#!/bin/sh

cd "$(dirname "$0")"

# Big core offline: this app runs on the little cores. MinUI.pak/launch.sh
# hands every pak over with cpu4 online (emulators need it); a 408 MHz big
# core adds nothing here, an offline one is power-gated.
echo 0 > /sys/devices/system/cpu/cpu4/online 2>/dev/null

./gametime.elf &> "$LOGS_PATH/gametime.txt"
