// wiz_modes.c - see wiz_modes.h.

#include "wiz_modes.h"

#include <string.h>

// Same order as wizard.c's mode_keys[]; the index is the mask bit.
static const char* const wiz_mode_words[] = {"usb", "hotspot", "wifi"};
#define WIZ_MODE_WORDS 3

// Longest csv accepted, including the terminator ("usb,hotspot,wifi" is 17).
#define WIZ_MODES_CSV_MAX 32

unsigned WizModes_parse(const char* csv) {
	if (!csv || !csv[0] || strlen(csv) >= WIZ_MODES_CSV_MAX)
		return 0;

	char buf[WIZ_MODES_CSV_MAX];
	strcpy(buf, csv);

	unsigned mask = 0;
	char* tok = buf;
	for (;;) {
		char* comma = strchr(tok, ',');
		if (comma)
			*comma = '\0';

		// an empty token ("usb,", ",usb", "usb,,wifi") is malformed
		int idx = -1;
		for (int i = 0; i < WIZ_MODE_WORDS; i++) {
			if (strcmp(tok, wiz_mode_words[i]) == 0) {
				idx = i;
				break;
			}
		}
		if (idx < 0 || (mask & (1u << idx)))
			return 0;
		mask |= 1u << idx;

		if (!comma)
			break;
		tok = comma + 1;
	}
	return mask;
}

int WizModes_rows(unsigned mask, int rows[3]) {
	int n = 0;
	for (int i = 0; i < WIZ_MODE_WORDS; i++) {
		if (mask & (1u << i))
			rows[n++] = i;
	}
	return n;
}

bool WizModes_singlePeer(const char* mode) {
	return mode && strcmp(mode, "usb") == 0;
}
