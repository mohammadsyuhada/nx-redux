#!/usr/bin/env bash
# Host tests for Dreamcast netplay in DC.pak: the real launch.sh (both
# platforms) with netplay.elf and minarch.elf stubbed on PATH. Checks what the
# wizard is asked, what the host serves, what the client plays on, the BIOS
# agreement, and the environment minarch and the core receive.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

mkdir -p "$TMP/bin"
# Stub wizard: records argv and the staged files it would serve; writes a
# session (role $NPELF_ROLE, peer caps $NPELF_PEER_CAPS); as a client, drops
# the host's files ($NPELF_FETCH, "name=content ...") into --fetch-to.
cat > "$TMP/bin/netplay.elf" <<'EOF'
#!/bin/sh
if [ "$1" = "--cleanup" ]; then echo cleanup >> "$T/cleanup"; exit 0; fi
printf '%s\n' "$@" > "$T/wizard_args"
serve=""; fetch=""; session=""; prev=""
for a in "$@"; do
	case "$prev" in
	--serve-dir) serve="$a" ;; --fetch-to) fetch="$a" ;; --session-file) session="$a" ;;
	esac
	prev="$a"
done
[ -n "$serve" ] && (cd "$serve" && for f in *; do [ -f "$f" ] && echo "$f=$(cat "$f")"; done) > "$T/served"
[ "${NPELF_RC:-0}" = 0 ] || exit "$NPELF_RC"
cat > "$session" <<SESH
NETPLAY_ROLE=${NPELF_ROLE}
NETPLAY_PEER_IP=10.0.0.2
NETPLAY_MODE=hotspot
NETPLAY_PLAYER=1
NETPLAY_NUM_PLAYERS=2
NETPLAY_GAME='stub'
NETPLAY_PREV_SSID=''
NETPLAY_PEER_CAPS='${NPELF_PEER_CAPS}'
SESH
if [ "$NPELF_ROLE" = client ]; then
	for pair in ${NPELF_FETCH:-}; do echo "${pair#*=}" > "$fetch/${pair%%=*}"; done
fi
exit 0
EOF
# Stub minarch: records the environment the core and minarch would see.
cat > "$TMP/bin/minarch.elf" <<'EOF'
#!/bin/sh
{
	for v in NX_CORE_NETPLAY NX_GGPO NX_GGPO_HOST NX_GGPO_SERVER NX_GGPO_HLE NETPLAY_SAVES_DIR NETPLAY_SYSTEM_DIR NETPLAY_ROLE NETPLAY_PEER_IP; do
		eval "echo $v=\${$v-UNSET}"
	done
	echo "ROM=$2"
} > "$T/minarch_env"
rm -rf "$T/seen_saves" "$T/seen_system"
[ -n "${NETPLAY_SAVES_DIR:-}" ] && cp -R "$NETPLAY_SAVES_DIR" "$T/seen_saves"
[ -n "${NETPLAY_SYSTEM_DIR:-}" ] && cp -R "$NETPLAY_SYSTEM_DIR" "$T/seen_system"
exit 0
EOF
chmod +x "$TMP/bin/netplay.elf" "$TMP/bin/minarch.elf"

card() { # fresh card: real Saves/Bios with this device's own files
	rm -rf "$TMP/sd" "$TMP/tmp" "$T"/* 2>/dev/null || true
	mkdir -p "$TMP/sd/Saves/DC/reicast" "$TMP/sd/Bios/DC" "$TMP/sd/.userdata/shared" "$TMP/sd/.userdata/tg5040" "$TMP/logs" "$TMP/tmp"
	echo MYCARD > "$TMP/sd/Saves/DC/Soulcalibur (USA).A1.bin"
	echo MYNV > "$TMP/sd/Bios/DC/dc_nvmem.bin"
	echo MYA2 > "$TMP/sd/Bios/DC/vmu_save_A2.bin"
	echo MYARC > "$TMP/sd/Saves/DC/reicast/mslug6.zip.nvmem"
	echo NAOMI > "$TMP/sd/Bios/DC/naomi.zip"
	echo AWBIOS > "$TMP/sd/Bios/DC/awbios.zip"
}
bios() { printf 'BIOS-%s' "$1" > "$TMP/sd/Bios/DC/dc_boot.bin"; }
fp() { md5sum "$TMP/sd/Bios/DC/dc_boot.bin" 2>/dev/null | cut -c1-12 || md5 -q "$TMP/sd/Bios/DC/dc_boot.bin" | cut -c1-12; }

T="$TMP/t"; mkdir -p "$T"; export T
launch() { # $1 = platform, $2 = ROM file name; env NETPLAY / NPELF_*
	rm -f "$T/wizard_args" "$T/minarch_env" "$T/served"
	[ -n "${NETPLAY:-}" ] && touch "$TMP/tmp/netplay_launch"
	PATH="$TMP/bin:$PATH" SDCARD_PATH="$TMP/sd" SAVES_PATH="$TMP/sd/Saves" \
	USERDATA_PATH="$TMP/sd/.userdata/tg5040" SHARED_USERDATA_PATH="$TMP/sd/.userdata/shared" \
	LOGS_PATH="$TMP/logs" NETPLAY_LAUNCH_FLAG="$TMP/tmp/netplay_launch" \
	NETPLAY_SESSION_FILE="$TMP/tmp/netplay_session" NX_DC_NETPLAY_TMP="$TMP/tmp" \
	NPELF_ROLE="${NPELF_ROLE:-host}" NPELF_PEER_CAPS="${NPELF_PEER_CAPS:-}" \
	NPELF_FETCH="${NPELF_FETCH:-}" NPELF_RC="${NPELF_RC:-0}" \
	sh "$ROOT/skeleton/SYSTEM/$1/paks/Emus/DC.pak/launch.sh" "/mnt/SDCARD/Roms/Dreamcast (DC)/$2" || true
}
env_is() { grep -qx "$1" "$T/minarch_env" 2>/dev/null; }

for PLAT in tg5040 tg5050; do
	# 1. plain launch: no wizard, no session environment
	card; bios A
	NETPLAY= launch $PLAT "Soulcalibur (USA).chd"
	[ ! -f "$T/wizard_args" ] || fail "$PLAT plain: wizard ran"
	env_is "NX_GGPO=UNSET" && env_is "NX_CORE_NETPLAY=UNSET" || fail "$PLAT plain: session env set"

	# 2. cancelled wizard: back to the list, the game never starts
	card; bios A
	NETPLAY=1 NPELF_RC=1 launch $PLAT "Soulcalibur (USA).chd"
	[ ! -f "$T/minarch_env" ] || fail "$PLAT cancel: minarch started"
	[ ! -f "$TMP/tmp/netplay_launch" ] || fail "$PLAT cancel: launch flag left behind"

	# 3. host, same BIOS on both sides: real BIOS, serves its own files, plays on them
	card; bios A
	NETPLAY=1 NPELF_ROLE=host NPELF_PEER_CAPS="dcbios=$(fp)" launch $PLAT "Soulcalibur (USA).chd"
	grep -qx -- "--caps" "$T/wizard_args" && grep -qx "dcbios=$(fp)" "$T/wizard_args" || fail "$PLAT host: caps not passed"
	grep -qx "Soulcalibur (USA)" "$T/wizard_args" || fail "$PLAT host: --game not the ROM stem"
	grep -qx "nxcard.bin=MYCARD" "$T/served" && grep -qx "nxnvmem.bin=MYNV" "$T/served" && grep -qx "nxa2.bin=MYA2" "$T/served" \
		|| fail "$PLAT host: served files wrong: $(cat "$T/served" 2>/dev/null)"
	env_is "NX_CORE_NETPLAY=1" && env_is "NX_GGPO=1" && env_is "NX_GGPO_HOST=1" && env_is "NX_GGPO_SERVER=10.0.0.2" \
		|| fail "$PLAT host: session env wrong: $(cat "$T/minarch_env")"
	env_is "NX_GGPO_HLE=0" || fail "$PLAT host: matching BIOS should be real"
	env_is "NETPLAY_SAVES_DIR=UNSET" && env_is "NETPLAY_SYSTEM_DIR=UNSET" || fail "$PLAT host: host must play on its real files"
	env_is "NETPLAY_ROLE=UNSET" && env_is "NETPLAY_PEER_IP=UNSET" || fail "$PLAT host: minarch link engine env leaked"
	[ "$(cat "$TMP/sd/.userdata/shared/DC-flycast/netplay-backup/nxcard.bin" 2>/dev/null)" = MYCARD ] || fail "$PLAT host: no backup"
	[ -f "$T/cleanup" ] || fail "$PLAT host: wizard cleanup not run"
	[ ! -e "$TMP/tmp/netplay-serve" ] || fail "$PLAT host: staged copies left after the session"

	# 4. BIOS differs / peer has none / we have none: HLE on this side
	card; bios A
	NETPLAY=1 NPELF_PEER_CAPS="dcbios=0123456789ab" launch $PLAT "Soulcalibur (USA).chd"
	env_is "NX_GGPO_HLE=1" || fail "$PLAT: different BIOS should force HLE"
	card; bios A
	NETPLAY=1 NPELF_PEER_CAPS="dcbios=none" launch $PLAT "Soulcalibur (USA).chd"
	env_is "NX_GGPO_HLE=1" || fail "$PLAT: peer without BIOS should force HLE"
	card; bios A
	NETPLAY=1 NPELF_PEER_CAPS="" launch $PLAT "Soulcalibur (USA).chd"
	env_is "NX_GGPO_HLE=1" || fail "$PLAT: peer without caps (old wizard) should force HLE"
	card
	NETPLAY=1 NPELF_PEER_CAPS="dcbios=none" launch $PLAT "Soulcalibur (USA).chd"
	grep -qx "dcbios=none" "$T/wizard_args" || fail "$PLAT: no BIOS should send dcbios=none"
	env_is "NX_GGPO_HLE=1" || fail "$PLAT: both without BIOS should be HLE"

	# 5. client: plays on the host's files in isolated copies; its own untouched
	card; bios A
	NETPLAY=1 NPELF_ROLE=client NPELF_PEER_CAPS="dcbios=$(fp)" \
	NPELF_FETCH="nxcard.bin=HOSTCARD nxnvmem.bin=HOSTNV nxa2.bin=HOSTA2" launch $PLAT "Soulcalibur (USA).chd"
	env_is "NX_GGPO_HOST=0" && env_is "NX_GGPO_HLE=0" || fail "$PLAT client: role/BIOS env wrong: $(cat "$T/minarch_env")"
	SD="$T/seen_saves"; SY="$T/seen_system"
	[ "$(cat "$SD/Soulcalibur (USA).A1.bin" 2>/dev/null)" = HOSTCARD ] || fail "$PLAT client: host card not in saves dir"
	[ "$(cat "$SY/dc_nvmem.bin" 2>/dev/null)" = HOSTNV ] && [ "$(cat "$SY/vmu_save_A2.bin" 2>/dev/null)" = HOSTA2 ] \
		|| fail "$PLAT client: host system files not in system dir"
	[ "$(cat "$SY/dc_boot.bin" 2>/dev/null)" = BIOS-A ] || fail "$PLAT client: agreed BIOS not in system dir"
	[ "$(cat "$TMP/sd/Saves/DC/Soulcalibur (USA).A1.bin")" = MYCARD ] && [ "$(cat "$TMP/sd/Bios/DC/dc_nvmem.bin")" = MYNV ] \
		|| fail "$PLAT client: own files changed"
	case "$(sed -n 's/^NETPLAY_SAVES_DIR=//p' "$T/minarch_env")" in "$TMP/tmp/"*) : ;; *) fail "$PLAT client: saves copy outside tmp" ;; esac
	# the session's copies are gone once the game has ended
	for d in netplay-serve netplay-fetch netplay-saves netplay-system; do
		[ ! -e "$TMP/tmp/$d" ] || fail "$PLAT client: $d left after the session"
	done

	# 6. client with HLE: no BIOS copy (the core must not find one)
	card; bios A
	NETPLAY=1 NPELF_ROLE=client NPELF_PEER_CAPS="dcbios=none" NPELF_FETCH="nxnvmem.bin=HOSTNV" launch $PLAT "Soulcalibur (USA).chd"
	SY="$T/seen_system"
	[ ! -e "$SY/dc_boot.bin" ] || fail "$PLAT client HLE: BIOS copied"
	# a host that had no card: the client plays on a blank one, not its own
	SD="$T/seen_saves"
	[ ! -e "$SD/Soulcalibur (USA).A1.bin" ] || fail "$PLAT client: card appeared without the host sending one"

	# 7. arcade client (upper-case .ZIP): host's arcade saves, arcade BIOS copied
	card; bios A
	NETPLAY=1 NPELF_ROLE=client NPELF_PEER_CAPS="dcbios=$(fp)" NPELF_FETCH="nxarc.nvmem=HOSTARC nxarc.eeprom=HOSTEE" launch $PLAT "MSLUG6.ZIP"
	SD="$T/seen_saves"; SY="$T/seen_system"
	[ "$(cat "$SD/reicast/MSLUG6.ZIP.nvmem" 2>/dev/null)" = HOSTARC ] && [ "$(cat "$SD/reicast/MSLUG6.ZIP.eeprom" 2>/dev/null)" = HOSTEE ] \
		|| fail "$PLAT arcade client: host arcade saves missing"
	[ "$(cat "$SY/naomi.zip" 2>/dev/null)" = NAOMI ] && [ "$(cat "$SY/awbios.zip" 2>/dev/null)" = AWBIOS ] \
		|| fail "$PLAT arcade client: arcade BIOS not copied"

	# 8. arcade host serves its arcade saves, not a card
	card; bios A
	NETPLAY=1 NPELF_ROLE=host NPELF_PEER_CAPS="dcbios=$(fp)" launch $PLAT "mslug6.zip"
	grep -qx "nxarc.nvmem=MYARC" "$T/served" || fail "$PLAT arcade host: arcade save not served"
	! grep -q "^nxcard.bin=" "$T/served" || fail "$PLAT arcade host: served a card"
done

cmp -s "$ROOT/skeleton/SYSTEM/tg5040/paks/Emus/DC.pak/nx_dc_netplay.sh" "$ROOT/skeleton/SYSTEM/tg5050/paks/Emus/DC.pak/nx_dc_netplay.sh" \
	|| fail "tg5040/tg5050 nx_dc_netplay.sh differ"

[ "$FAIL" = 0 ] && echo "test-dc-netplay-sh: OK"
exit "$FAIL"
