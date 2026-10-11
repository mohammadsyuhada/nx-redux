#!/bin/sh
# gpu-shader-boost.sh - run the GPU at full clock while a minarch shader is on.
#
# Source (don't exec) from a pak's launch.sh after ROM="$1" is set and before
# the minarch.elf line, with EMU_EXE (the core name) set:
#     . "$SHARED_SYSTEM_PATH/bin/gpu-shader-boost.sh"
#     nx_gpu_shader_boost
# nx_shader_on tells the pak itself whether the game starts with a shader on.
#
# Why: on the Smart Pro S (tg5050) the GPU's simple_ondemand governor stays at
# its 150 MHz floor under a shader (sampled 2026-10-11: mGBA FireRed with
# lcd-perfect, every sample 150 MHz, 38.8 fps). The performance governor holds
# it at the top clock.
#
# What it does: when the game starts with a shader on, writes performance to
# the GPU governor before minarch starts. Either way it exports
# NX_GPU_SHADER_BOOST=<the governor before any boost>, which opts minarch in to
# switching the governor itself when the shader is turned on or off in the
# in-game menu (and tells it what to go back to). Devices with no GPU governor
# node (tg5040's GPU has a fixed clock): no-op, nothing exported.
#
# The shader count is read the way minarch resolves it: the game's config if
# there is one, else the console's; below those the pak's default and then the
# system config. A "-key = value" line is a locked key, read the same.

NX_GPU_GOVERNOR_PATH="${NX_GPU_GOVERNOR_PATH:-/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor}"

# nx_cfg_value <file> <key>: the key's value in a minarch cfg; fails when the
# file or the key is missing
nx_cfg_value() {
	[ -f "$1" ] || return 1
	sed -n "s/^-\{0,1\}$2 = //p" "$1" | tail -n 1 | tr -d '\r' | grep .
}

# nx_shader_count: minarch_nrofshaders for this game ("off", "1", "2" or "3")
nx_shader_count() {
	_dir="$USERDATA_PATH/$EMU_TAG-$EMU_EXE"
	_dev="${DEVICE:+-$DEVICE}"
	_user="$_dir/minarch$_dev.cfg"
	[ -f "$_dir/$(basename "$ROM")$_dev.cfg" ] && _user="$_dir/$(basename "$ROM")$_dev.cfg"
	_default="$(dirname "$0")/default.cfg"
	[ -n "$DEVICE" ] && [ -f "$(dirname "$0")/default-$DEVICE.cfg" ] && _default="$(dirname "$0")/default-$DEVICE.cfg"
	_system="$SYSTEM_PATH/system.cfg"
	[ -n "$DEVICE" ] && [ -f "$SYSTEM_PATH/system-$DEVICE.cfg" ] && _system="$SYSTEM_PATH/system-$DEVICE.cfg"
	for _cfg in "$_user" "$_default" "$_system"; do
		nx_cfg_value "$_cfg" minarch_nrofshaders && return 0
	done
	echo off
}

# nx_shader_on: true when this game starts with a shader on (a pak can also use it
# for its own CPU choice)
nx_shader_on() {
	case "$(nx_shader_count)" in
	1 | 2 | 3) return 0 ;;
	esac
	return 1
}

nx_gpu_shader_boost() {
	[ -w "$NX_GPU_GOVERNOR_PATH" ] || return 0
	_base="$(cat "$NX_GPU_GOVERNOR_PATH" 2>/dev/null)"
	[ -n "$_base" ] || return 0
	export NX_GPU_SHADER_BOOST="$_base"
	nx_shader_on && echo performance > "$NX_GPU_GOVERNOR_PATH" 2>/dev/null
	return 0
}
