// Host test for netplay-wizard/wiz_modes.c: the --modes csv a launcher passes
// to limit the wizard's Connection menu.
#include <assert.h>
#include <stdio.h>
#include "../../netplay-wizard/wiz_modes.h"
int main(void) {
	assert(WizModes_parse("usb") == 0x1u);
	assert(WizModes_parse("usb,wifi") == 0x5u);
	assert(WizModes_parse("wifi,hotspot,usb") == 0x7u);
	assert(WizModes_parse("") == 0);
	assert(WizModes_parse(NULL) == 0);
	assert(WizModes_parse("usb,bluetooth") == 0);
	assert(WizModes_parse("usb,usb") == 0);
	assert(WizModes_parse("usb,") == 0);
	int r[3];
	assert(WizModes_rows(0x5u, r) == 2 && r[0] == 0 && r[1] == 2);
	assert(WizModes_rows(0x1u, r) == 1 && r[0] == 0);
	assert(WizModes_rows(WIZ_MODES_ALL, r) == 3 && r[2] == 2);
	puts("wiz_modes: ok");
	return 0;
}
