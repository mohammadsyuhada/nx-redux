#!/bin/sh
# Sourced (". $SYSTEM_PATH/bin/nx_button_layout.sh") by pak launch scripts.
# Exports NX_BUTTON_LAYOUT=nintendo|xbox from Settings > System > Button
# layout (minuisettings.txt buttonlayout=, read through nextval.elf, which
# lives in $SYSTEM_PATH/bin — on PATH for every pak). Anything unexpected
# (nextval missing, empty output) means nintendo, the physical layout.
# Consumers: DraStic's SDL hook, the N64/DC in-game overlays, the N64 and DC
# bindings transforms, PortMaster's gamecontrollerdb selection.
_nxbl=$(nextval.elf buttonlayout 2>/dev/null | sed -n 's/.*"buttonlayout": \([0-9]*\).*/\1/p')
if [ "$_nxbl" = "1" ]; then
    NX_BUTTON_LAYOUT=xbox
else
    NX_BUTTON_LAYOUT=nintendo
fi
export NX_BUTTON_LAYOUT
unset _nxbl
