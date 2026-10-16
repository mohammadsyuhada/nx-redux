#!/bin/sh

EMU_EXE=gpsp

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
CORE="$CORES_PATH/${EMU_EXE}_libretro.so"
# gpSP can't run a Classic NES Series / Famicom Mini cart (a 4 MiB dump dies with
# a bus error, a 1 MiB cut is black); mGBA can, so such a cart runs MGBA.pak's
# core. Known by the GBA header: the fixed 0x96 at 0xB2 and a game code at 0xAC,
# four letters or digits starting with F (Pac-Man: FP7E). A bare .gba only: a
# .zip/.7z is unpacked by minarch. The tag stays GBA (minarch takes it from the
# ROM folder), so the save stays in Saves/GBA (same format in both cores) and
# states and settings go to GBA-mgba. Set before the netplay and shader steps,
# which read EMU_EXE.
CORE_NOTE=""
MGBA_CORE="$(dirname "$0")/../MGBA.pak/mgba_libretro.so"
case "$ROM" in
*.gba | *.GBA)
	if [ -f "$MGBA_CORE" ] && [ "$(dd if="$ROM" bs=1 skip=178 count=1 2>/dev/null)" = "$(printf '\226')" ]; then
		AZ09=ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789
		case "$(dd if="$ROM" bs=1 skip=172 count=4 2>/dev/null)" in
		F[$AZ09][$AZ09][$AZ09])
			EMU_EXE=mgba
			CORE="$MGBA_CORE"
			CORE_NOTE="Classic NES Series cart: running mgba instead of gpsp"
			;;
		esac
	fi
	;;
esac
. "$SHARED_SYSTEM_PATH/bin/netplay-prelaunch.sh"
# All four little cores, uncapped, with the big cluster gated (scripts/bench
# 2026-10-11, FireRed mid-game): on cpu4 alone 9.8% of frames were late, on
# cpu0-3 none. The launcher takes cpu2/3 offline and cpu4 back up on exit.
# minarch's startup pin to cpu4-7 then fails (logged, harmless).
# A USB Cable link runs there too: measured with mGBA (MGBA.pak), which is
# heavier than gpSP; minarch runs at Performance while link traffic flows.
echo 1 > /sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 > /sys/devices/system/cpu/cpu3/online 2>/dev/null
echo 0 > /sys/devices/system/cpu/cpu4/online 2>/dev/null
# While a shader is on, the GPU runs at performance (gpu-shader-boost.sh) and
# minarch's Auto holds the CPU at its top (1416 MHz on the little cluster):
# lcd-perfect was 50.3 fps with 72.8% of frames late on v2.1.0, 0.1% late so.
export NX_SHADER_CPU_MAX=1
. "$SHARED_SYSTEM_PATH/bin/gpu-shader-boost.sh"
nx_gpu_shader_boost
{
	[ -n "$CORE_NOTE" ] && echo "$CORE_NOTE"
	minarch.elf "$CORE" "$ROM"
} > "$LOGS_PATH/$EMU_TAG.txt" 2>&1
if [ -f /tmp/netplay_session ]; then
	netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
fi
