#include "../../nextui/emulist_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void prefix_cuts_after_last_paren(void) {
	char out[256];
	Emulist_collatedPrefix("/R/Game Boy (GB)", out, sizeof(out));
	assert(strcmp(out, "/R/Game Boy (") == 0);
	Emulist_collatedPrefix("/R/Sega (Japan) (MD)", out, sizeof(out));
	assert(strcmp(out, "/R/Sega (Japan) (") == 0); // the LAST paren
	Emulist_collatedPrefix("/R/Ports", out, sizeof(out));
	assert(strcmp(out, "/R/Ports") == 0); // no paren: the whole path
}

static void parse_row_round_trip(void) {
	char line[256];
	Emulist_formatRow("/R/Game Boy (GB)", "Game Boy", 12, line, sizeof(line));
	assert(strcmp(line, "/R/Game Boy (GB)\tGame Boy\t12\n") == 0);
	line[strlen(line) - 1] = '\0'; // the reader strips the newline
	char *path, *name;
	int count = -1;
	assert(Emulist_parseRow(line, &path, &name, &count));
	assert(strcmp(path, "/R/Game Boy (GB)") == 0 && strcmp(name, "Game Boy") == 0 && count == 12);
}

static void parse_row_name_with_paren(void) {
	char line[] = "/R/Sega Genesis (MD)\tSega Genesis (Mega Drive)\t0";
	char *path, *name;
	int count = -1;
	assert(Emulist_parseRow(line, &path, &name, &count));
	assert(strcmp(name, "Sega Genesis (Mega Drive)") == 0 && count == 0);
}

static void parse_row_rejects(void) {
	char *path = NULL, *name = NULL;
	int count = 7;
	char two_col[] = "/R/GBA (GBA)\tGame Boy Advance"; // pre-count cache
	assert(!Emulist_parseRow(two_col, &path, &name, &count));
	char junk[] = "/R/GBA (GBA)\tGame Boy Advance\t12x";
	assert(!Emulist_parseRow(junk, &path, &name, &count));
	char neg[] = "/R/GBA (GBA)\tGame Boy Advance\t-1";
	assert(!Emulist_parseRow(neg, &path, &name, &count));
	char empty[] = "/R/GBA (GBA)\tGame Boy Advance\t";
	assert(!Emulist_parseRow(empty, &path, &name, &count));
	char extra[] = "/R/GBA (GBA)\tGame Boy Advance\t3\t4";
	assert(!Emulist_parseRow(extra, &path, &name, &count));
	char no_name[] = "/R/GBA (GBA)\t\t3";
	assert(!Emulist_parseRow(no_name, &path, &name, &count));
	char no_path[] = "\tGame Boy Advance\t3";
	assert(!Emulist_parseRow(no_path, &path, &name, &count));
	char huge[] = "/R/GBA (GBA)\tGame Boy Advance\t99999999999999999999";
	assert(!Emulist_parseRow(huge, &path, &name, &count));
	assert(count == 7 && path == NULL && name == NULL); // outputs untouched on failure
}

static void collate_siblings_and_quirks(void) {
	const char* consoles[] = {"/R/Game Boy (GB)", "/R/Ports", "/R/Ports2", "/R/Arcade"};
	const char* folders[] = {"/R/Game Boy (GB)", "/R/game boy (SGB)", "/R/Ports", "/R/Ports2", "/R/Arcade",
							 "/R/GBA (GBA)"};
	const int counts[] = {5, 3, 2, 4, 9, 100};
	int out[4];
	Emulist_collateCounts(consoles, 4, folders, counts, 6, out);
	assert(out[0] == 8); // GB + SGB combine under "Game Boy (", case-insensitively
	assert(out[1] == 2); // Ports is not Ports2: a row with no paren only collates itself
	assert(out[2] == 4); // nor the other way round
	assert(out[3] == 9); // a folder with no paren
}

static void collate_no_paren_is_exact(void) {
	assert(Emulist_collates("/Roms/GB", "/Roms/GB"));
	assert(Emulist_collates("/Roms/GB", "/roms/gb")); // case-insensitive
	assert(!Emulist_collates("/Roms/GB", "/Roms/GBA (GBA)"));
	assert(!Emulist_collates("/Roms/GB", "/Roms/GBC"));
	assert(!Emulist_collates("/Roms/PS", "/Roms/PSP (PPSSPP)"));
	assert(!Emulist_collates("/Roms/PS", "/Roms/PSX"));
	assert(!Emulist_collates("/Roms/PS", "/Roms/PS/sub")); // not a subfolder either
	assert(!Emulist_collates("/Roms/PS", "/Roms/PS "));	   // a trailing space is another name
	// the same name plus a tag still merges (a tag-less survivor of the display-name dedupe)
	assert(Emulist_collates("/Roms/Ports", "/Roms/Ports (PORTS)"));
	assert(Emulist_collates("/Roms/Ports", "/Roms/ports (PORTS)"));
	assert(!Emulist_collates("/Roms/Ports", "/Roms/Ports(PORTS)")); // no space: another name
	// with a paren the prefix rule is unchanged
	assert(Emulist_collates("/Roms/Game Boy (GB)", "/Roms/Game Boy (SGB)"));
	assert(!Emulist_collates("/Roms/Game Boy (GB)", "/Roms/Game Boy Advance (GBA)"));
	assert(!Emulist_collates(NULL, "/Roms/GB") && !Emulist_collates("/Roms/GB", NULL));
}

static void format_row_blanks_name_controls(void) {
	char line[256];
	Emulist_formatRow("/R/GBA (GBA)", "Game\tBoy\r\nAdvance", 3, line, sizeof(line));
	assert(strcmp(line, "/R/GBA (GBA)\tGame Boy  Advance\t3\n") == 0);
	line[strlen(line) - 1] = '\0';
	char *path, *name;
	int count = -1;
	assert(Emulist_parseRow(line, &path, &name, &count)); // reads back as one row
	assert(strcmp(name, "Game Boy  Advance") == 0 && count == 3);
	// cut short: blanks only what was written
	char small[16];
	Emulist_formatRow("/R/X", "a\tb\tc\td\te\tf\tg", 1, small, sizeof(small));
	assert(strcmp(small, "/R/X\ta b c d e ") == 0);
	char field[] = "x\ty\nz";
	Emulist_blankField(field, 2); // only the first len bytes
	assert(strcmp(field, "x y\nz") == 0);
	Emulist_blankField(NULL, 4);
}

static void collate_sibling_indexed_twice(void) {
	// getRoms indexes a dedupe loser under each survivor that collates it: its folder count is the sum
	const char* consoles[] = {"/R/Sega Genesis (GPGX)"};
	const char* folders[] = {"/R/Sega Genesis (GPGX)", "/R/Sega Genesis (MD)", "/R/Sega Genesis (MD)"};
	const int counts[] = {1, 2, 2};
	int out[1];
	Emulist_collateCounts(consoles, 1, folders, counts, 3, out);
	assert(out[0] == 5);
}

static void adjust_clamps_and_matches(void) {
	int count = 3;
	Emulist_adjustCount("/R/Game Boy (GB)", "/R/Game Boy (SGB)", 2, &count);
	assert(count == 1); // a sibling's delete lowers the merged row
	Emulist_adjustCount("/R/Game Boy (GB)", "/R/GBA (GBA)", 1, &count);
	assert(count == 1); // another console: untouched
	Emulist_adjustCount("/R/Game Boy (GB)", "/R/Game Boy (GB)", 5, &count);
	assert(count == 0); // clamped at 0
	count = 4;
	Emulist_adjustCount("/R/Ports", "/R/Ports2", 1, &count);
	assert(count == 4); // another console that merely starts the same: untouched
	Emulist_adjustCount("/R/Ports", "/R/Ports (PORTS)", 1, &count);
	assert(count == 3); // its tagged twin follows the collation
}

int main(void) {
	prefix_cuts_after_last_paren();
	parse_row_round_trip();
	parse_row_name_with_paren();
	parse_row_rejects();
	collate_siblings_and_quirks();
	collate_sibling_indexed_twice();
	collate_no_paren_is_exact();
	format_row_blanks_name_controls();
	adjust_clamps_and_matches();
	printf("test_emulist_model: ok\n");
	return 0;
}
