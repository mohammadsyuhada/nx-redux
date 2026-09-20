#!/bin/sh

cd $(dirname "$0")

# Big core offline: this app runs on the little cores. MinUI.pak/launch.sh
# hands every pak over with cpu4 online (emulators need it); a 408 MHz big
# core adds nothing here, an offline one is power-gated. Little cores capped below.
echo 0 > /sys/devices/system/cpu/cpu4/online 2>/dev/null
echo 1032000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

HOME="$SDCARD_PATH"
CFG="tg5050.cfg"
# Settings > System > Button layout. NextCommander bakes the Nintendo face
# indices (B=0 A=1 Y=2 X=3) into the binary; under Xbox we hand it a cfg that
# moves confirm to the bottom button and back to the right one, X/Y likewise.
[ -f "$SYSTEM_PATH/bin/nx_button_layout.sh" ] && . "$SYSTEM_PATH/bin/nx_button_layout.sh"
if [ "$NX_BUTTON_LAYOUT" = "xbox" ]; then
    {
        cat "$CFG"
        printf '\nkey_open=0\nkey_parent=1\nkey_operation=2\nkey_system=3\n'
    } > /tmp/files.cfg
    CFG=/tmp/files.cfg
fi
./NextCommander --config "$CFG" &> "$LOGS_PATH/files.txt"
