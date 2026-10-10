#!/usr/bin/env bash
# Every Emus pak that ships a `netplay` marker (Y NETPLAY hint + wizard) must
# run a core that minarch's link backends accept (checkCoreLinkSupport:
# lockstep netplay, GBA Link, GB Link); otherwise NetplayBoot refuses the
# session at launch and both players are stranded. Cores that run their own
# netplay are listed in SELF_NETPLAY.
set -euo pipefail
cd "$(dirname "$0")/../.."

NETPLAY=workspace/all/netplay
# DC: flycast's GGPO session; N64: mupen64plus' own netplay against the pak's
# relay (both NX_CORE_NETPLAY, core_netplay.h)
SELF_NETPLAY="flycast mupen64plus_next"

supported=$(grep -ho 'strcasecmp(core_name, "[^"]*")' \
	"$NETPLAY/netplay.c" "$NETPLAY/gbalink.c" "$NETPLAY/gblink.c" | sed 's/.*"\(.*\)")/\1/' | sort -u)
[ -n "$supported" ] || { echo "FAIL: found no supported cores (did the check move?)" >&2; exit 1; }

fail=0
checked=0
for marker in skeleton/SYSTEM/*/paks/Emus/*.pak/netplay; do
	pak=$(dirname "$marker")
	core=$(sed -n 's/^EMU_EXE=//p' "$pak/launch.sh" | head -1)
	if [ -z "$core" ]; then
		# Standalone emulators bring their own netplay.
		grep -q 'minarch.elf' "$pak/launch.sh" && { echo "FAIL: $pak: minarch pak without EMU_EXE" >&2; fail=1; }
		continue
	fi
	checked=$((checked + 1))
	if ! printf '%s\n' "$supported" $SELF_NETPLAY | grep -qx "$core"; then
		echo "FAIL: $pak ships a netplay marker but core '$core' has no netplay backend" >&2
		fail=1
	fi
done

[ "$checked" -gt 0 ] || { echo "FAIL: no netplay paks found" >&2; exit 1; }
[ "$fail" -eq 0 ] || exit 1
echo "PASS: netplay paks ($checked) all have a netplay backend"
