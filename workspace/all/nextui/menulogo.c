// Console folder -> its logo: the user's <folder>/.media/logo.png, else the bundled res/menu/menu_logo_<id>.png.
// SDL-free, host-tested by common/tests/test_menulogo.c.

#include "menulogo.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

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
	{"DCX", "dcx"}, // Dreamcast Lite: the Dreamcast logo with a LITE badge
	{"PS", "ps"},
	{"PSX", "ps"}, // SwanStation, sharing the PlayStation logo
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
	{"EXTRAS", "extras"},
	{"PUAE", "amiga"},
	{"CPC", "cpc"},
	{"C64", "c64"},
	{"C128", "c128"},
	{"PET", "pet"},
	{"PLUS4", "plus4"},
	{"VIC", "vic20"},
	{"MSX", "msx"},
	{"DOS", "dos"},
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

// Whether each console folder has its own logo, looked up once (an access() per folder) instead of every frame
#define USER_LOGO_SLOTS 64
static struct {
	char folder[512];
	bool has;
} user_logos[USER_LOGO_SLOTS];
static int user_logo_count = 0;

static bool hasUserLogo(const char* folder_path) {
	for (int i = 0; i < user_logo_count; i++)
		if (strcmp(user_logos[i].folder, folder_path) == 0)
			return user_logos[i].has;
	char path[600];
	snprintf(path, sizeof(path), "%s/.media/logo.png", folder_path);
	bool has = access(path, F_OK) == 0;
	if (strlen(folder_path) < sizeof(user_logos[0].folder)) {
		if (user_logo_count == USER_LOGO_SLOTS)
			user_logo_count = 0; // more consoles than slots: start over (still once per folder per pass)
		snprintf(user_logos[user_logo_count].folder, sizeof(user_logos[0].folder), "%s", folder_path);
		user_logos[user_logo_count++].has = has;
	}
	return has;
}

const char* MenuLogo_fileFor(const char* folder_path, char* out, size_t size) {
	if (!folder_path || !folder_path[0] || !out || size == 0)
		return NULL;
	if (hasUserLogo(folder_path)) {
		if ((size_t)snprintf(out, size, "%s/.media/logo.png", folder_path) < size)
			return out;
	}
	const char* slash = strrchr(folder_path, '/');
	const char* id = MenuLogo_idForFolder(slash ? slash + 1 : folder_path);
	if (!id)
		return NULL;
	return (size_t)snprintf(out, size, "menu_logo_%s.png", id) < size ? out : NULL;
}

void MenuLogo_forget(void) {
	user_logo_count = 0;
}
