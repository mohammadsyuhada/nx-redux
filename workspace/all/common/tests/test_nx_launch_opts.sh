#!/bin/sh
# Host test for skeleton/SYSTEM/<plat>/bin/nx_launch_opts.sh (Launch Options):
# INI layering (per-game override > global > auto), unknown tokens, CRLF,
# section scoping, and the per-platform sysfs writes against a fake tree
# (NX_SYSFS_ROOT). Runs once per skeleton copy; the copies must be identical.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../../../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

cmp -s "$root/skeleton/SYSTEM/tg5040/bin/nx_launch_opts.sh" \
	"$root/skeleton/SYSTEM/tg5050/bin/nx_launch_opts.sh" ||
	fail "tg5040 and tg5050 nx_launch_opts.sh differ"

cpu="$tmp/sys/devices/system/cpu"
gpu="$tmp/sys/devices/platform/soc@3000000/1800000.gpu/devfreq/1800000.gpu/governor"
ud="$tmp/userdata"
dir="$ud/PORTS-launch"
rom="/x/Roms/Ports (PORTS)/Don't Starve.v2.sh"
game="$dir/Don't Starve.v2.sh.ini"

# Fresh fake sysfs: online = 7 (sentinel) on cpu1-7, cpu0 has no online node,
# every cpufreq/gpu file = UNCHANGED. Also wipes the INI dir.
seed() {
	rm -rf "$tmp/sys" "$ud"
	mkdir -p "$dir" "${gpu%/*}"
	i=0
	while [ $i -le 7 ]; do
		mkdir -p "$cpu/cpu$i/cpufreq"
		[ $i -gt 0 ] && echo 7 >"$cpu/cpu$i/online"
		for f in scaling_governor scaling_min_freq scaling_max_freq; do
			echo UNCHANGED >"$cpu/cpu$i/cpufreq/$f"
		done
		i=$((i + 1))
	done
	echo UNCHANGED >"$gpu"
}

expect() { # file expected label
	got=$(cat "$1")
	[ "$got" = "$2" ] || fail "$helper: $3: $1 = '$got', want '$2'"
}
expect_policy() { # cpuN governor khz label
	expect "$cpu/$1/cpufreq/scaling_governor" "$2" "$4"
	expect "$cpu/$1/cpufreq/scaling_min_freq" "$3" "$4"
	expect "$cpu/$1/cpufreq/scaling_max_freq" "$3" "$4"
}
expect_untouched_cpufreq() { # cpuN label
	expect_policy "$1" UNCHANGED UNCHANGED "$2"
}
expect_online() { # first last value label
	i=$1
	while [ "$i" -le "$2" ]; do
		expect "$cpu/cpu$i/online" "$3" "$4"
		i=$((i + 1))
	done
}

# run PLATFORM -> sources the helper in a subshell, loads, applies.
# Prints "CORES CPU GPU" on the last line.
run() {
	PLATFORM=$1 USERDATA_PATH=$ud NX_SYSFS_ROOT=$tmp sh -c '
		. "$0"
		nx_launch_opts_load PORTS "$1" >/dev/null
		nx_launch_opts_apply >/dev/null
		echo "$NX_LO_CORES $NX_LO_CPU $NX_LO_GPU"
	' "$helper" "$rom"
}

for plat in tg5040 tg5050; do
	helper="$root/skeleton/SYSTEM/$plat/bin/nx_launch_opts.sh"
	[ -f "$helper" ] || fail "$helper missing"
	sh -n "$helper" || fail "$helper does not parse"

	# 1. no INI files: all auto, nothing written on either platform
	for p in tg5040 tg5050; do
		seed
		out=$(run $p)
		[ "$out" = "auto auto auto" ] || fail "$helper: case1 $p: got '$out'"
		expect_online 1 7 7 "case1 $p"
		i=0
		while [ $i -le 7 ]; do expect_untouched_cpufreq cpu$i "case1 $p"; i=$((i + 1)); done
		expect "$gpu" UNCHANGED "case1 $p"
	done

	# 2. precedence: override cpu beats global cpu; global gpu shows through
	seed
	printf '[launch]\ncores = auto\ncpu = normal\ngpu = performance\n' >"$dir/launch.ini"
	printf '[launch]\ncpu = powersave\n' >"$game"
	out=$(run tg5050)
	[ "$out" = "auto powersave performance" ] || fail "$helper: case2: got '$out'"

	# 3. garbage token -> auto; CRLF line still parsed; no-space '=' accepted
	seed
	printf '[launch]\r\ncpu = turbo\r\ncores = all8\r\ngpu=powersave\r\n' >"$dir/launch.ini"
	out=$(run tg5050)
	[ "$out" = "all8 auto powersave" ] || fail "$helper: case3: got '$out'"
	# garbage in the override falls through to the global value
	printf '[launch]\ncpu = 9000\n' >"$game"
	printf '[launch]\ncpu = normal\n' >"$dir/launch.ini"
	out=$(run tg5050)
	[ "$out" = "auto normal auto" ] || fail "$helper: case3b: got '$out'"

	# 4. tg5050 little4 + performance: cpu4-7 offline, policy4 untouched
	seed
	printf '[launch]\ncores = little4\ncpu = performance\n' >"$game"
	run tg5050 >/dev/null
	expect_online 1 3 1 "case4"
	expect_online 4 7 0 "case4"
	expect_policy cpu0 schedutil 2160000 "case4"
	expect_untouched_cpufreq cpu4 "case4"
	expect "$gpu" UNCHANGED "case4"

	# 5. tg5050 little4big2 + gpu powersave (+ cpu normal hits policy4 too)
	seed
	printf '[launch]\ncores = little4big2\ncpu = normal\ngpu = powersave\n' >"$game"
	run tg5050 >/dev/null
	expect_online 1 5 1 "case5"
	expect_online 6 7 0 "case5"
	expect "$gpu" simple_ondemand "case5"
	expect_policy cpu0 schedutil 1680000 "case5"
	expect_policy cpu4 schedutil 1680000 "case5"
	# gpu performance governor
	seed
	printf '[launch]\ngpu = performance\n' >"$game"
	run tg5050 >/dev/null
	expect "$gpu" performance "case5b"
	expect_online 1 7 7 "case5b"

	# 6. tg5040 normal: cpu0 1608000; cores/gpu ignored
	seed
	printf '[launch]\ncores = little4\ncpu = normal\ngpu = powersave\n' >"$game"
	out=$(run tg5040)
	[ "$out" = "little4 normal powersave" ] || fail "$helper: case6: got '$out'"
	expect_policy cpu0 schedutil 1608000 "case6"
	expect_online 1 7 7 "case6"
	expect_untouched_cpufreq cpu4 "case6"
	expect "$gpu" UNCHANGED "case6"

	# 7. keys outside [launch] are ignored
	seed
	printf '[other]\ncpu = performance\n[launch]\ngpu = powersave\n[x]\ncores = all8\n' >"$game"
	out=$(run tg5050)
	[ "$out" = "auto auto powersave" ] || fail "$helper: case7: got '$out'"
	expect_untouched_cpufreq cpu0 "case7"

	# 8. apply logs one line to stdout
	seed
	printf '[launch]\ncpu = powersave\n' >"$game"
	log=$(PLATFORM=tg5050 USERDATA_PATH=$ud NX_SYSFS_ROOT=$tmp sh -c '. "$0"; nx_launch_opts_load PORTS "$1"; nx_launch_opts_apply' "$helper" "$rom")
	[ "$log" = "launch options: cores=auto cpu=powersave gpu=auto" ] || fail "$helper: case8 log: '$log'"

	# 9b. tg5050 little2: only cpu0+cpu1 online, policy4 untouched
	seed
	printf '[launch]\ncores = little2\ncpu = powersave\n' >"$game"
	out=$(run tg5050)
	[ "$out" = "little2 powersave auto" ] || fail "$helper: case9b: got '$out'"
	expect_online 1 1 1 "case9b"
	expect_online 2 7 0 "case9b"
	expect_untouched_cpufreq cpu4 "case9b"

	# 9. multi-disc folder game: the launch gets disc 1, the key is the folder-named .m3u
	seed
	mdir="$tmp/roms/Game (USA)"
	mkdir -p "$mdir"
	: >"$mdir/Game (USA).m3u"
	printf '[launch]\ncpu = normal\n' >"$dir/Game (USA).m3u.ini"
	out=$(PLATFORM=tg5050 USERDATA_PATH=$ud NX_SYSFS_ROOT=$tmp sh -c '. "$0"; nx_launch_opts_load PORTS "$1"; echo "$NX_LO_CPU"' "$helper" "$mdir/Game (USA) (Disc 1).chd")
	[ "$out" = "normal" ] || fail "$helper: case9 m3u key: got '$out'"
done
echo "test_nx_launch_opts: OK"
