#!/bin/sh
# Sourced (". $SYSTEM_PATH/bin/nx_button_layout.sh") by pak launch scripts.
# Exports NX_BUTTON_LAYOUT=nintendo|xbox from Settings > System > Button
# layout (minuisettings.txt buttonlayout=, read through nextval.elf, which
# lives in $SYSTEM_PATH/bin — on PATH for every pak). Anything unexpected
# (nextval missing, empty output) means nintendo, the physical layout.
# Consumers: DraStic's SDL hook, the N64/DC in-game overlays, the N64
# bindings transform, PortMaster's gamecontrollerdb selection, the Files pak
# (NextCommander key overrides).
# The `|| _nxbl=` keeps a failed command substitution (nextval missing or
# crashing) from aborting a consumer sourced under `set -e`/`pipefail`.
_nxbl=$(nextval.elf buttonlayout 2>/dev/null | sed -n 's/.*"buttonlayout": \([0-9]*\).*/\1/p') || _nxbl=
if [ "$_nxbl" = "1" ]; then
    NX_BUTTON_LAYOUT=xbox
else
    NX_BUTTON_LAYOUT=nintendo
fi
export NX_BUTTON_LAYOUT
unset _nxbl
