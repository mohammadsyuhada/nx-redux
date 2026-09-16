#!/bin/sh
# netplay-prelaunch.sh - shared pre-launch netplay wizard step for minarch paks.
#
# Source (don't exec) from a pak's launch.sh after ROM="$1" is set, before the
# minarch.elf line:
#     . "$SHARED_SYSTEM_PATH/bin/netplay-prelaunch.sh"
# Pair it with a teardown block after minarch.elf exits:
#     if [ -f /tmp/netplay_session ]; then
#         netplay.elf --cleanup >> "$LOGS_PATH/netplay-wizard.txt" 2>&1
#     fi
#
# Plain launches (no /tmp/netplay_launch flag): no-op.
# Netplay launches: consume the flag, run the wizard, and on success export
# NETPLAY_ROLE / NETPLAY_PEER_IP / NETPLAY_MODE for minarch's boot-time engine
# start. Env vars on purpose: /tmp/netplay_session can survive an OSD/power-quit,
# env cannot, so a stale session file never starts netplay.
# On wizard cancel/error, exit the SOURCING launch.sh with status 0 - MinUI
# restarts nextui and the user is back at the game list; the emulator never
# starts (and never falls through to a peerless single-player launch).
#
# Save sync (lockstep cores only): the lockstep link engine mirrors the
# emulator's full state, but PicoDrive (Sega) and PCSX (PlayStation) leave cart
# SRAM / the memory card OUT of that state, so the two devices must also START
# from the same save or they desync. And a client that adopts the host's save
# must not have it written back over its own file. Both are solved the same way
# the Dreamcast pak already does it: the host serves its save read-only and the
# client plays on a copy in an isolated dir (NETPLAY_SAVES_DIR, read by
# minarch's Core_open). GB/GBC/GBA are LINK-CABLE cores (gambatte/gpsp) and are
# excluded on purpose - link play (Pokemon trading) needs DISTINCT saves.
#
# The wizard's transfer only accepts filenames matching [A-Za-z0-9._-], but a
# libretro .srm is named after the ROM (spaces, brackets), so the real file is
# staged under a fixed safe name to be served, and the client renames the fetch
# back to what minarch expects.
#
# NETPLAY_LAUNCH_FLAG / NETPLAY_SESSION_FILE and the save dirs are overridable
# for tests only.
NETPLAY_LAUNCH_FLAG="${NETPLAY_LAUNCH_FLAG:-/tmp/netplay_launch}"
NETPLAY_SESSION_FILE="${NETPLAY_SESSION_FILE:-/tmp/netplay_session}"
NETPLAY_SAVE_STAGE_DIR="${NETPLAY_SAVE_STAGE_DIR:-/tmp/netplay-serve}"
NETPLAY_SAVE_DATA_DIR="${NETPLAY_SAVE_DATA_DIR:-/tmp/netplay-saves}"

if [ -f "$NETPLAY_LAUNCH_FLAG" ]; then
	rm -f "$NETPLAY_LAUNCH_FLAG"
	NETPLAY_GAME_NAME=$(basename "$ROM")
	NETPLAY_GAME_NAME="${NETPLAY_GAME_NAME%.*}"

	# Lockstep cores sync saves; the two link-cable cores never do.
	case "${EMU_EXE:-}" in
		gambatte|gpsp) NETPLAY_SYNC_SAVES=0 ;;
		*)             NETPLAY_SYNC_SAVES=1 ;;
	esac

	# Stage the local save under a safe name so it can be served if we host, and
	# give the client a clean dir to fetch into. Both dirs live in /tmp (thrown
	# away next reboot); rebuilt each session so a stale copy never leaks in.
	# Paths here are space-free by construction (Saves/<TAG>, /tmp), so the
	# unquoted expansion of NP_WIZ_SYNC_ARGS below splits into args correctly.
	NP_WIZ_SYNC_ARGS=""
	if [ "$NETPLAY_SYNC_SAVES" = "1" ]; then
		NP_REAL_SAVES="$SAVES_PATH/$EMU_TAG"
		rm -rf "$NETPLAY_SAVE_STAGE_DIR" "$NETPLAY_SAVE_DATA_DIR"
		mkdir -p "$NETPLAY_SAVE_STAGE_DIR" "$NETPLAY_SAVE_DATA_DIR"
		for ext in srm sav; do
			if [ -f "$NP_REAL_SAVES/$NETPLAY_GAME_NAME.$ext" ]; then
				cp -f "$NP_REAL_SAVES/$NETPLAY_GAME_NAME.$ext" \
					"$NETPLAY_SAVE_STAGE_DIR/nxsave.$ext"
			fi
		done
		NP_WIZ_SYNC_ARGS="--serve-dir $NETPLAY_SAVE_STAGE_DIR --fetch-to $NETPLAY_SAVE_DATA_DIR --fetch-files nxsave.srm,nxsave.sav"
	fi

	netplay.elf --game "$NETPLAY_GAME_NAME" --session-file "$NETPLAY_SESSION_FILE" \
		$NP_WIZ_SYNC_ARGS \
		> "$LOGS_PATH/netplay-wizard.txt" 2>&1
	if [ $? -ne 0 ]; then
		exit 0
	fi
	[ -f "$NETPLAY_SESSION_FILE" ] || exit 0
	. "$NETPLAY_SESSION_FILE"
	export NETPLAY_ROLE NETPLAY_PEER_IP NETPLAY_MODE

	# Client: the wizard fetched the host's save under the fixed staged name.
	# Rename it to what minarch derives (<rom>.<ext>) and point minarch's save
	# path at the isolated dir, so the player's own Saves/<tag> is never read or
	# written. The host plays on its real save (it "brings the save").
	if [ "$NETPLAY_SYNC_SAVES" = "1" ] && [ "$NETPLAY_ROLE" = "client" ]; then
		for ext in srm sav; do
			if [ -f "$NETPLAY_SAVE_DATA_DIR/nxsave.$ext" ]; then
				mv -f "$NETPLAY_SAVE_DATA_DIR/nxsave.$ext" \
					"$NETPLAY_SAVE_DATA_DIR/$NETPLAY_GAME_NAME.$ext"
			fi
		done
		export NETPLAY_SAVES_DIR="$NETPLAY_SAVE_DATA_DIR"
	fi
fi
