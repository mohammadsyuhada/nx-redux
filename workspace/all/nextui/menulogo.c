// Console folder -> bundled logo id (res/menu/menu_logo_<id>.png). SDL-free,
// host-tested by common/tests/test_menulogo.c.

#include "menulogo.h"

#include <string.h>
#include <strings.h>

static const struct {
	const char* tag;
	const char* id;
} tag_logos[] = {
	{"GB", "gb"},
	{"GBC", "gbc"},
	{"GBA", "gba"},
	{"MGBA", "gba"},
	{"SGB", "sgb"},
	{"FC", "fc"},
	{"FDS", "fds"},
	{"SFC", "sfc"},
	{"SUPA", "sfc"},
	{"N64", "n64"},
	{"NDS", "nds"},
	{"3DS", "3ds"},
	{"VB", "vb"},
	{"PKM", "pkm"},
	{"MD", "md"},
	{"SMS", "sms"},
	{"GG", "gg"},
	{"SG1000", "sg1000"},
	{"SEGACD", "segacd"},
	{"32X", "s32x"},
	{"DC", "dc"},
	{"PS", "ps"},
	{"PSP", "psp"},
	{"PCE", "pce"},
	{"NGP", "ngp"},
	{"NGPC", "ngpc"},
	{"WSC", "wsc"},
	{"LYNX", "lynx"},
	{"A2600", "a2600"},
	{"A5200", "a5200"},
	{"A7800", "a7800"},
	{"COLECO", "coleco"},
	{"P8", "p8"},
	{"PRBOOM", "prboom"},
	{"FBN", "fbn"},
	{"PORTS", "ports"},
};

// GPGX serves several Sega systems from one tag: pick by the display name.
static const struct {
	const char* name_part;
	const char* id;
} gpgx_logos[] = {
	{"Genesis", "md"},
	{"Mega Drive", "md"},
	{"Master System", "sms"},
	{"Game Gear", "gg"},
	{"Sega CD", "segacd"},
	{"Mega-CD", "segacd"},
	{"SG-1000", "sg1000"},
};

const char* MenuLogo_idForFolder(const char* folder_name) {
	if (!folder_name)
		return NULL;
	const char* open = strrchr(folder_name, '(');
	const char* close = open ? strchr(open, ')') : NULL;
	if (!open || !close || close - open - 1 <= 0 || close - open - 1 >= 16)
		return NULL;
	char tag[16];
	memcpy(tag, open + 1, (size_t)(close - open - 1));
	tag[close - open - 1] = '\0';

	if (strcasecmp(tag, "GPGX") == 0) {
		for (size_t i = 0; i < sizeof(gpgx_logos) / sizeof(gpgx_logos[0]); i++)
			if (strstr(folder_name, gpgx_logos[i].name_part))
				return gpgx_logos[i].id;
		return NULL;
	}
	for (size_t i = 0; i < sizeof(tag_logos) / sizeof(tag_logos[0]); i++)
		if (strcasecmp(tag, tag_logos[i].tag) == 0)
			return tag_logos[i].id;
	return NULL;
}
