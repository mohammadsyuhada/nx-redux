#!/bin/sh
# SoC temperature: the hottest CPU/GPU thermal zone, picked by type since the
# devices name them differently (Brick: cpu_/gpu_thermal_zone; Smart Pro S:
# cpul_/cpub_/gpu_thermal_zone). The battery sensor this used to show stays near
# room temperature while the SoC runs hot enough to throttle. Falls back to the
# battery zone only when no CPU/GPU zone exists.
THERMAL_ROOT="${THERMAL_ROOT:-/sys/class/thermal}"
OSD_TMP="${OSD_TMP:-/tmp/trimui_osd}"
soc=0
battery=0
for zone in "$THERMAL_ROOT"/thermal_zone*; do
	t=$(cat "$zone/temp" 2>/dev/null) || continue
	case "$(cat "$zone/type" 2>/dev/null)" in
	cpu* | gpu*) [ "$t" -gt "$soc" ] && soc=$t ;;
	*battery*) battery=$t ;;
	esac
done
[ "$soc" -gt 0 ] || soc=$battery
str=$((soc / 1000)).$((soc % 1000 / 100))
mkdir -p "$OSD_TMP/toggle_temperature"
printf "%s" "$str" > "$OSD_TMP/toggle_temperature/temperature"
