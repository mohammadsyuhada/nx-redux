#!/bin/sh
# EXTRAS platform runtime dispatcher (Task 15). $1 is the launched entry's
# own script (e.g. Roms/Xtra Games (EXTRAS)/Gen1recomp.sh). Two branches:
#   - native: the entry's own script carries a "# NX_RUNTIME: native"
#     marker line in its first 20 lines -> exec it directly, no wrapper.
#     Native entries own their ENTIRE runtime themselves (env vars,
#     background services, cleanup) - this dispatcher does nothing else
#     for them. Declared in the entry's meta.txt via runtime=native.
#     gen1recomp is native as of 2026-08-10 (its bundled LOVE runtime
#     resolves every lib from the firmware, so PortMaster isn't needed).
#   - ports (default - no marker, e.g. gen1recomp's meta.txt declares
#     runtime=ports explicitly): run through the proven PORTS runtime,
#     as today.
# The ports runtime is Emus/PORTS.pak/launch.sh (a copy of
# ports_launch.sh the portmaster Xtras entry's install.sh creates) - it
# only exists once PortMaster is installed, which the ports branch needs
# anyway. The ports branch is a defensive fallback only: native standalone games
# (payload in .data/<id>/) are the sole expected runtime under "Xtra Games
# (EXTRAS)", and PortMaster-dependent extras install into the normal
# Roms/Ports (PORTS) tree from their own install.sh instead of here.

if head -20 "$1" 2>/dev/null | grep -q "^# NX_RUNTIME: native"; then
    exec "$1"
fi

if [ -f "$SDCARD_PATH/Emus/PORTS.pak/launch.sh" ]; then
    exec "$SDCARD_PATH/Emus/PORTS.pak/launch.sh" "$@"
fi

echo "extras_games_launch.sh: PORTS runtime not found - install PortMaster from Xtras" >&2
exit 1
