#!/bin/sh
# EXTRAS platform runtime dispatcher. $1 is the launched entry's
# own script (e.g. Roms/Xtra Games (EXTRAS)/Gen1recomp.sh). Two branches:
#   - native: the entry's own script carries a "# NX_RUNTIME: native"
#     marker line in its first 20 lines -> exec it directly, no wrapper.
#     Native entries own their ENTIRE runtime themselves (env vars,
#     background services, cleanup) - this dispatcher does nothing else
#     for them. Declared in the entry's meta.txt via runtime=native.
#     gen1recomp is native as of 2026-08-10 (its bundled LOVE runtime
#     resolves every lib from the firmware, so PortMaster isn't needed).
#   - ports (default - no marker): run through the PORTS runtime.
# The ports runtime is Emus/PORTS.pak/launch.sh (a copy of
# ports_launch.sh the portmaster Xtras entry's install.sh creates) - it
# only exists once PortMaster is installed, which the ports branch needs
# anyway. ports_launch.sh resolves the .ports data dir from the rom's own
# directory, so it works for "Xtra Games (EXTRAS)" unchanged.

if head -20 "$1" 2>/dev/null | grep -q "^# NX_RUNTIME: native"; then
    exec "$1"
fi

if [ -f "$SDCARD_PATH/Emus/PORTS.pak/launch.sh" ]; then
    exec "$SDCARD_PATH/Emus/PORTS.pak/launch.sh" "$@"
fi

echo "extras_games_launch.sh: PORTS runtime not found - install PortMaster from Xtras" >&2
exit 1
