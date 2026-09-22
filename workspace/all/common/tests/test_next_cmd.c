// Host unit test for libgametimedb/next_cmd.h: recovering the ROM path from
// the single-quoted launch command in /tmp/next. Built with ASan by
// run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../libgametimedb/next_cmd.h"

#define STR_MAX 256

// The reader libgametimedb shipped before 2026-09-22: text between the last
// two quote characters. Kept here to pin down the regression it fixes.
static int old_lastQuoted(const char* in, char* out) {
	char cmd[STR_MAX];
	strcpy(cmd, in);
	char* ptr = strrchr(cmd, '\'');
	if (ptr)
		*ptr = '\0';
	ptr = strrchr(cmd, '\'');
	if (!ptr)
		return 0;
	strcpy(out, ptr + 1);
	return 1;
}

static void check(const char* cmd, const char* want) {
	char got[STR_MAX];
	int ok = NextCmd_lastWord(cmd, got, sizeof(got));
	if (!ok || strcmp(got, want) != 0) {
		fprintf(stderr, "FAIL: cmd=[%s]\n  want=[%s]\n  got =[%s] (rc %d)\n", cmd, want, got, ok);
		assert(0);
	}
}

int main(void) {
	// Regression: apostrophe in the title. escapeSingleQuotes turns ' into '\''.
	const char* links = "'/mnt/SDCARD/Emus/GB.pak/launch.sh' '/mnt/SDCARD/Roms/Game Boy (GB)/Link'\\''s Awakening.gb'";
	char old[STR_MAX];
	assert(old_lastQuoted(links, old));
	assert(strcmp(old, "s Awakening.gb") == 0); // what the tracker used to look up
	check(links, "/mnt/SDCARD/Roms/Game Boy (GB)/Link's Awakening.gb");

	// Plain path, and the doubled-space folder from the GBC report.
	check("'/x/GBC.pak/launch.sh' '/mnt/SDCARD/Roms/Game Boy Color (GBC)/Pokemon - Crystal.gbc'",
		  "/mnt/SDCARD/Roms/Game Boy Color (GBC)/Pokemon - Crystal.gbc");
	check("'/x/GBC.pak/launch.sh' '/mnt/SDCARD/Roms/Game Boy Color  (GBC)/Pokemon  Crystal.gbc'",
		  "/mnt/SDCARD/Roms/Game Boy Color  (GBC)/Pokemon  Crystal.gbc");

	// Two apostrophes, and one at the very end of the name.
	check("'/x/l.sh' '/r/Tom'\\''s and Jerry'\\''s.gb'", "/r/Tom's and Jerry's.gb");
	check("'/x/l.sh' '/r/Rock'\\'''", "/r/Rock'");

	// A pak launch has a single word: the old reader returned it too.
	check("'/mnt/SDCARD/Tools/tg5040/Files.pak/launch.sh'", "/mnt/SDCARD/Tools/tg5040/Files.pak/launch.sh");
	check("'/mnt/SDCARD/Tools/tg5040/Emulator Settings.pak/launch.sh'",
		  "/mnt/SDCARD/Tools/tg5040/Emulator Settings.pak/launch.sh");

	// Trailing newline / whitespace, tabs between words, unquoted words.
	check("'/x/l.sh' '/r/a.gb'\n", "/r/a.gb");
	check("'/x/l.sh'\t'/r/a.gb'  ", "/r/a.gb");
	check("/x/l.sh /r/plain.gb", "/r/plain.gb");
	check("/x/l.sh /r/back\\ slashed.gb", "/r/back slashed.gb");

	// Empty / blank command: nothing to look up.
	char got[STR_MAX];
	assert(NextCmd_lastWord("", got, sizeof(got)) == 0);
	assert(NextCmd_lastWord("   \n", got, sizeof(got)) == 0);
	assert(NextCmd_lastWord(NULL, got, sizeof(got)) == 0);

	// Output is truncated, never overrun (ASan would catch a write past 8).
	char tiny[8];
	assert(NextCmd_lastWord("'/x/l.sh' '/r/a-very-long-name.gb'", tiny, sizeof(tiny)) == 1);
	assert(strlen(tiny) == 7);

	printf("test_next_cmd: all passed\n");
	return 0;
}
