#!/usr/bin/env bash
# Host test for the PortMaster-refresh block appended to each platform's
# install/update.sh. PortMaster is an Xtras catalog entry whose installer
# copies its GUI pak (Tools/PortMaster.pak) and the Ports console runner
# (Emus/PORTS.pak/launch.sh) as user-level copies a firmware update never
# touched, so they go stale (field case 2026-09-16: a Brick still ran the
# Aug-20 portmaster.elf and the old marker-based ports runner). The block
# re-syncs those copies from the freshly unpacked catalog whenever PortMaster
# is installed. This extracts the marked block from each update.sh and runs it
# under sh against a fake card. Busybox-compatible constructs only in the
# block under test.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
FAIL=0
fail() { echo "FAIL: $*" >&2; FAIL=1; }

# Build a card with the fresh catalog pak/ dir populated.
seed_catalog() { # $1 = card root, $2 = platform tag for content
    cat="$1/.system/paks/Tools/Xtras.pak/catalog/portmaster/pak"
    mkdir -p "$cat"
    printf 'CATALOG launch %s\n'        "$2" > "$cat/launch.sh"
    printf 'CATALOG ports_launch %s\n'  "$2" > "$cat/ports_launch.sh"
    printf 'CATALOG elf %s\n'           "$2" > "$cat/portmaster.elf"
}
mark_installed() { # $1 = card root
    mkdir -p "$1/Emus/shared/PortMaster"
    printf 'ports-master\n' > "$1/Emus/shared/PortMaster/version"
}
seed_stale_tools() { # $1 = card root
    mkdir -p "$1/Tools/PortMaster.pak"
    for f in launch.sh ports_launch.sh portmaster.elf; do
        printf 'STALE %s\n' "$f" > "$1/Tools/PortMaster.pak/$f"
        chmod 644 "$1/Tools/PortMaster.pak/$f"
    done
}
seed_stale_ports() { # $1 = card root
    mkdir -p "$1/Emus/PORTS.pak"
    printf 'STALE ports launch\n' > "$1/Emus/PORTS.pak/launch.sh"
    chmod 644 "$1/Emus/PORTS.pak/launch.sh"
}

for PLAT in tg5040 tg5050; do
    UPDATE="$ROOT/workspace/$PLAT/install/update.sh"
    BLOCK="$(sed -n '/^# --- portmaster-refresh-begin/,/^# --- portmaster-refresh-end/p' "$UPDATE")"
    if [ -z "$BLOCK" ]; then
        fail "$PLAT: portmaster-refresh block not found in update.sh"
        continue
    fi

    run_block() { # $1 = card root ; echoes nothing, returns block exit status
        SDCARD_PATH="$1" sh -c "$BLOCK"
    }

    CAT="/.system/paks/Tools/Xtras.pak/catalog/portmaster/pak"

    # --- case (a): installed, all four copies stale (mode 644) -------------
    card="$TMP/$PLAT-a"; mkdir -p "$card"
    seed_catalog "$card" "$PLAT"; mark_installed "$card"
    seed_stale_tools "$card"; seed_stale_ports "$card"
    rc=0; run_block "$card" >/dev/null || rc=$?
    [ "$rc" = 0 ] || fail "$PLAT (a): block exited $rc"
    cmp -s "$card/Tools/PortMaster.pak/launch.sh"       "$card$CAT/launch.sh"       || fail "$PLAT (a): Tools launch.sh not refreshed"
    cmp -s "$card/Tools/PortMaster.pak/ports_launch.sh" "$card$CAT/ports_launch.sh" || fail "$PLAT (a): Tools ports_launch.sh not refreshed"
    cmp -s "$card/Tools/PortMaster.pak/portmaster.elf"  "$card$CAT/portmaster.elf"  || fail "$PLAT (a): Tools portmaster.elf not refreshed"
    cmp -s "$card/Emus/PORTS.pak/launch.sh"             "$card$CAT/ports_launch.sh" || fail "$PLAT (a): PORTS launch.sh not refreshed from ports_launch.sh"
    for f in launch.sh ports_launch.sh portmaster.elf; do
        [ -x "$card/Tools/PortMaster.pak/$f" ] || fail "$PLAT (a): Tools/$f not executable"
    done
    [ -x "$card/Emus/PORTS.pak/launch.sh" ] || fail "$PLAT (a): PORTS launch.sh not executable"

    # --- case (b): not installed (no version file) -> nothing changes ------
    card="$TMP/$PLAT-b"; mkdir -p "$card"
    seed_catalog "$card" "$PLAT"
    seed_stale_tools "$card"; seed_stale_ports "$card"
    rc=0; run_block "$card" >/dev/null || rc=$?
    [ "$rc" = 0 ] || fail "$PLAT (b): block exited $rc"
    grep -q '^STALE launch.sh$' "$card/Tools/PortMaster.pak/launch.sh"   || fail "$PLAT (b): Tools launch.sh changed while not installed"
    grep -q '^STALE ports launch$' "$card/Emus/PORTS.pak/launch.sh"      || fail "$PLAT (b): PORTS launch.sh changed while not installed"
    [ ! -x "$card/Tools/PortMaster.pak/launch.sh" ] || fail "$PLAT (b): Tools launch.sh made executable while not installed"

    # --- case (c): installed but Tools/PortMaster.pak absent ---------------
    card="$TMP/$PLAT-c"; mkdir -p "$card"
    seed_catalog "$card" "$PLAT"; mark_installed "$card"
    seed_stale_ports "$card"    # note: no Tools/PortMaster.pak dir
    rc=0; run_block "$card" >/dev/null || rc=$?
    [ "$rc" = 0 ] || fail "$PLAT (c): block exited $rc"
    [ ! -e "$card/Tools/PortMaster.pak" ] || fail "$PLAT (c): Tools/PortMaster.pak created when absent"
    cmp -s "$card/Emus/PORTS.pak/launch.sh" "$card$CAT/ports_launch.sh" || fail "$PLAT (c): PORTS launch.sh not refreshed when Tools absent"
    [ -x "$card/Emus/PORTS.pak/launch.sh" ] || fail "$PLAT (c): PORTS launch.sh not executable when Tools absent"

    # --- case (d): catalog dir absent -> nothing changes, exit 0 -----------
    card="$TMP/$PLAT-d"; mkdir -p "$card"
    mark_installed "$card"      # installed, but no catalog pak/ dir at all
    seed_stale_tools "$card"; seed_stale_ports "$card"
    rc=0; run_block "$card" >/dev/null || rc=$?
    [ "$rc" = 0 ] || fail "$PLAT (d): block exited $rc with catalog absent"
    grep -q '^STALE launch.sh$' "$card/Tools/PortMaster.pak/launch.sh" || fail "$PLAT (d): Tools launch.sh changed with catalog absent"
    grep -q '^STALE ports launch$' "$card/Emus/PORTS.pak/launch.sh"    || fail "$PLAT (d): PORTS launch.sh changed with catalog absent"
done

[ "$FAIL" = 0 ] && echo "test-update-portmaster-refresh: OK"
exit "$FAIL"
