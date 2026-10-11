#!/bin/sh
# nx_launch_opts.sh: per-game Launch Options (CPU cores / CPU mode / GPU mode).
# Sourced (busybox sh), never run. Byte-identical on tg5040 and tg5050; it
# branches on $PLATFORM.
#
#   nx_launch_opts_load TAG ROM_PATH
#     Sets NX_LO_CORES, NX_LO_CPU and NX_LO_GPU. Each comes from the per-game
#     override $USERDATA_PATH/TAG-launch/<rom basename>.ini, then the global
#     $USERDATA_PATH/TAG-launch/launch.ini, then "auto". Only keys inside the
#     [launch] section count. An unknown token is ignored (falls through to the
#     next layer, finally auto), so a missing or corrupt file never fails.
#
#   nx_launch_opts_apply
#     Writes the non-auto values for $PLATFORM under ${NX_SYSFS_ROOT:-}/sys.
#     Call it AFTER the pak's own CPU/GPU setup. Auto writes nothing. Every
#     write tolerates a missing node. The MinUI.pak loop restores cores and
#     cpufreq after the command; nextui's gpuEnterMenu restores the GPU.
#
# Tokens: cores auto|little2|little4|little4big2|all8 (tg5050 only)
#         cpu   auto|powersave|normal|performance
#         gpu   auto|powersave|performance (tg5050 only)

NX_LO_CORES=auto
NX_LO_CPU=auto
NX_LO_GPU=auto

# Print "key=value" for every key in the [launch] section of $1 (CR and
# surrounding whitespace stripped). Prints nothing for a missing file.
_nxlo_read_ini() {
	[ -f "$1" ] || return 0
	awk '
		{ gsub(/\r/, ""); sub(/^[ \t]+/, ""); sub(/[ \t]+$/, "") }
		/^\[/ { s = $0; sub(/^\[[ \t]*/, "", s); sub(/[ \t]*\].*$/, "", s); insec = (s == "launch"); next }
		!insec { next }
		{
			i = index($0, "="); if (!i) next
			k = substr($0, 1, i - 1); v = substr($0, i + 1)
			sub(/[ \t]+$/, "", k); sub(/^[ \t]+/, "", v)
			print k "=" v
		}
	' "$1" 2>/dev/null
}

# Apply one key=value line, accepting only valid tokens.
_nxlo_take() {
	case "$1" in
	cores) case "$2" in auto | little2 | little4 | little4big2 | all8) NX_LO_CORES=$2 ;; esac ;;
	cpu) case "$2" in auto | powersave | normal | performance) NX_LO_CPU=$2 ;; esac ;;
	gpu) case "$2" in auto | powersave | performance) NX_LO_GPU=$2 ;; esac ;;
	esac
}

nx_launch_opts_load() {
	NX_LO_CORES=auto
	NX_LO_CPU=auto
	NX_LO_GPU=auto
	_nxlo_dir="$USERDATA_PATH/$1-launch"
	# Per-game key: the basename with extension, collapsed to the folder-named
	# .m3u for multi-disc folder games (a launch gets the first disc, the
	# editor the .m3u), as minarch's alt_name does.
	_nxlo_rdir="${2%/*}"
	if [ -f "$_nxlo_rdir/${_nxlo_rdir##*/}.m3u" ]; then
		_nxlo_game="$_nxlo_dir/${_nxlo_rdir##*/}.m3u.ini"
	else
		_nxlo_game="$_nxlo_dir/${2##*/}.ini"
	fi
	# Global first, then the override, so the per-game value wins.
	for _nxlo_f in "$_nxlo_dir/launch.ini" "$_nxlo_game"; do
		_nxlo_kv=$(_nxlo_read_ini "$_nxlo_f")
		[ -n "$_nxlo_kv" ] || continue
		while IFS= read -r _nxlo_line; do
			_nxlo_take "${_nxlo_line%%=*}" "${_nxlo_line#*=}"
		done <<EOF
$_nxlo_kv
EOF
	done
	unset _nxlo_dir _nxlo_rdir _nxlo_game _nxlo_f _nxlo_kv _nxlo_line
}

_nxlo_put() {
	echo "$1" >"$2" 2>/dev/null
}

# Set cpu$1..cpu$2 online state to $3 (never cpu0).
_nxlo_online() {
	_nxlo_i=$1
	while [ "$_nxlo_i" -le "$2" ]; do
		_nxlo_put "$3" "$_nxlo_cpu/cpu$_nxlo_i/online"
		_nxlo_i=$((_nxlo_i + 1))
	done
}

# governor + min-max-min on one policy dir (see tg5050 platform.c setPolicyRange).
_nxlo_policy() {
	_nxlo_put schedutil "$1/cpufreq/scaling_governor"
	_nxlo_put "$2" "$1/cpufreq/scaling_min_freq"
	_nxlo_put "$2" "$1/cpufreq/scaling_max_freq"
	_nxlo_put "$2" "$1/cpufreq/scaling_min_freq"
}

nx_launch_opts_apply() {
	_nxlo_cpu="${NX_SYSFS_ROOT:-}/sys/devices/system/cpu"
	_nxlo_gpu="${NX_SYSFS_ROOT:-}/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor"
	case "$PLATFORM" in
	tg5040)
		case "$NX_LO_CPU" in
		powersave) _nxlo_khz=1200000 ;;
		normal) _nxlo_khz=1608000 ;;
		performance) _nxlo_khz=2000000 ;;
		*) _nxlo_khz= ;;
		esac
		[ -n "$_nxlo_khz" ] && _nxlo_policy "$_nxlo_cpu/cpu0" "$_nxlo_khz"
		echo "launch options: cores=n/a cpu=$NX_LO_CPU gpu=n/a"
		;;
	tg5050)
		# 1. cores (cpu0 is the boot CPU and is never touched)
		case "$NX_LO_CORES" in
		little2) _nxlo_online 1 1 1; _nxlo_online 2 7 0 ;;
		little4) _nxlo_online 1 3 1; _nxlo_online 4 7 0 ;;
		little4big2) _nxlo_online 1 5 1; _nxlo_online 6 7 0 ;;
		all8) _nxlo_online 1 7 1 ;;
		esac
		# 2. cpu: every online policy (cpu4 only while it is up)
		case "$NX_LO_CPU" in
		powersave) _nxlo_khz=1200000 ;;
		normal) _nxlo_khz=1680000 ;;
		performance) _nxlo_khz=2160000 ;;
		*) _nxlo_khz= ;;
		esac
		if [ -n "$_nxlo_khz" ]; then
			_nxlo_policy "$_nxlo_cpu/cpu0" "$_nxlo_khz"
			[ "$(cat "$_nxlo_cpu/cpu4/online" 2>/dev/null)" = 1 ] &&
				_nxlo_policy "$_nxlo_cpu/cpu4" "$_nxlo_khz"
		fi
		# 3. gpu
		case "$NX_LO_GPU" in
		powersave) _nxlo_put simple_ondemand "$_nxlo_gpu" ;;
		performance) _nxlo_put performance "$_nxlo_gpu" ;;
		esac
		echo "launch options: cores=$NX_LO_CORES cpu=$NX_LO_CPU gpu=$NX_LO_GPU"
		;;
	*)
		echo "launch options: unsupported platform '$PLATFORM', nothing applied"
		;;
	esac
	unset _nxlo_cpu _nxlo_gpu _nxlo_khz _nxlo_i
	return 0
}
