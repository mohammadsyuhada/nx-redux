// Host-compiled unit tests for sync_proto (no device toolchain).
// Build & run: workspace/all/sync/tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sync_proto.h"

static SyncPeer parse(const char* s) {
	SyncPeer p;
	sync_parse_payload(s, strlen(s), &p);
	return p;
}

static void test_parse_flags(void) {
	SyncPeer p = parse("192.168.1.5");
	assert(!strcmp(p.ip, "192.168.1.5") && !p.roms && !p.client_only && !p.filters);
	p = parse("192.168.1.5:0");
	assert(!strcmp(p.ip, "192.168.1.5") && !p.roms && !p.client_only && !p.filters);
	p = parse("192.168.1.5:R");
	assert(!strcmp(p.ip, "192.168.1.5") && p.roms && !p.client_only && !p.filters);
	p = parse("10.99.0.1:c");
	assert(!strcmp(p.ip, "10.99.0.1") && !p.roms && p.client_only && p.filters);
	p = parse("10.99.0.1:C");
	assert(!strcmp(p.ip, "10.99.0.1") && p.roms && p.client_only && p.filters);
	p = parse("192.168.1.5:x"); // unknown flag: ROMs off, as old firmware reads it
	assert(!strcmp(p.ip, "192.168.1.5") && !p.roms && !p.client_only);
}

static void test_parse_rejects(void) {
	const char* bad[] = {"", "garbage", "192.168.1.5:RR", "192.168.1.256", "1.2.3.4;reboot",
						 "1.2.3.4 :R", "::R", "999999999999999999999999999999999999999999999999999999999999999999", NULL};
	for (int i = 0; bad[i]; i++) {
		SyncPeer p;
		assert(!sync_parse_payload(bad[i], strlen(bad[i]), &p));
		assert(p.ip[0] == '\0' && !p.roms && !p.filters);
	}
}

static void test_format_roundtrip(void) {
	char buf[128];
	int n = sync_format_payload(buf, sizeof(buf), "HELLO_TRIMUI_SYNC:", "192.168.1.7", true);
	assert(n == (int)strlen("HELLO_TRIMUI_SYNC:192.168.1.7:R") + 3);
	// What old firmware sees: the C string only.
	assert(!strcmp(buf, "HELLO_TRIMUI_SYNC:192.168.1.7:R"));
	SyncPeer p;
	const char* pay = buf + strlen("HELLO_TRIMUI_SYNC:");
	assert(sync_parse_payload(pay, n - (pay - buf), &p));
	assert(!strcmp(p.ip, "192.168.1.7") && p.roms && !p.client_only && p.filters);
	n = sync_format_payload(buf, sizeof(buf), "", "10.99.0.2", false);
	assert(sync_parse_payload(buf, n, &p) && !p.roms && p.filters);
	// A wrong trailer is not a filtering peer.
	assert(sync_parse_payload("1.2.3.4:0\0v3", 12, &p) && !p.filters);
	assert(sync_format_payload(buf, 12, "", "192.168.1.7", false) == -1);
}

static void test_roles(void) {
	const char* lo = "10.99.0.1";
	const char* hi = "10.99.0.2";
	// neither client-only: lower IP serves
	assert(sync_is_server(false, false, lo, hi) && !sync_is_server(false, false, hi, lo));
	// both client-only: same IP rule
	assert(sync_is_server(true, true, lo, hi) && !sync_is_server(true, true, hi, lo));
	// peer client-only: we serve whatever the IPs
	assert(sync_is_server(false, true, lo, hi) && sync_is_server(false, true, hi, lo));
	// we are client-only: we never serve
	assert(!sync_is_server(true, false, lo, hi) && !sync_is_server(true, false, hi, lo));
}

static void test_cable_net(void) {
	assert(sync_ip_is_cable("10.99.0.1") && sync_ip_is_cable("10.99.0.2") && sync_ip_is_cable("10.99.0.3"));
	assert(!sync_ip_is_cable("10.99.0.4") && !sync_ip_is_cable("192.168.1.2") && !sync_ip_is_cable("x"));
}

static void test_filter_strings(void) {
	char buf[1024];
	assert(sync_shared_daemon_filter(buf, sizeof(buf)) > 0);
	const char* d0 = "- ._* - .DS_Store - /music-player/ ";
	assert(!strncmp(buf, d0, strlen(d0)));
	assert(strstr(buf, "+ /.minui/ + /.minui/*/ + /.minui/*/** - /.* + /*-*/ + /*-*/** - *"));
	assert(buf[strlen(buf) - 1] == '*');
	assert(sync_shared_client_args(buf, sizeof(buf)) > 0);
	const char* c0 = "--filter='- ._*' --filter='- .DS_Store' --filter='- /music-player/' ";
	assert(!strncmp(buf, c0, strlen(c0)));
	assert(strstr(buf, "--filter='- *'"));
	assert(sync_shared_client_args(buf, 20) == -1);
}

static void test_console_display(void) {
	char d[64];
	assert(sync_console_display("Game Boy Advance (GBA)", d, sizeof(d)) && !strcmp(d, "Game Boy Advance"));
	assert(sync_console_display("Pokémon mini (PKM)", d, sizeof(d)) && !strcmp(d, "Pokémon mini"));
	assert(sync_console_display("Sega CD (GPGX)", d, sizeof(d)) && !strcmp(d, "Sega CD"));
	assert(sync_console_display("Name(TAG) ", d, sizeof(d)) && !strcmp(d, "Name"));
	const char* bad[] = {"Game Boy Advance", ".media", "(GBA)", "Name ()", "Name (A(B))", "Name (GBA) x", NULL};
	for (int i = 0; bad[i]; i++)
		assert(!sync_console_display(bad[i], d, sizeof(d)));
	assert(!sync_console_display("Game Boy Advance (GBA)", d, 8)); // doesn't fit
}

static void test_siblings(void) {
	assert(sync_is_sibling("Game Boy Advance (GBA)", "Game Boy Advance (MGBA)"));
	assert(sync_is_sibling("Sega CD (GPGX)", "Sega CD (SEGACD)"));
	assert(!sync_is_sibling("Sega CD (GPGX)", "Sega Genesis (GPGX)"));			  // same tag, other console
	assert(!sync_is_sibling("Game Boy Advance (GBA)", "Game Boy Advance (GBA)")); // itself
	assert(!sync_is_sibling("Game Boy (GB)", "Game Boy Advance (GBA)"));
	assert(!sync_is_sibling(".media", ".media"));
	assert(!sync_is_sibling("Game Boy", "Game Boy (GB)"));
}

static void test_list_line(void) {
	char n[256];
	bool dir = false;
	assert(sync_list_line_name("drwxr-xr-x          4,096 2026/10/15 12:00:00 Game Boy Advance (GBA)", n, sizeof(n), &dir));
	assert(!strcmp(n, "Game Boy Advance (GBA)") && dir);
	assert(sync_list_line_name("-rw-r--r--    123,456,789 2026/10/15 12:00:00 Foo  Bar.gba", n, sizeof(n), &dir));
	assert(!strcmp(n, "Foo  Bar.gba") && !dir);
	// --8-bit-output: non-ASCII names arrive raw
	assert(sync_list_line_name("drwxr-xr-x          4,096 2026/10/15 12:00:00 Pokémon mini (PKM)", n, sizeof(n), &dir));
	assert(!strcmp(n, "Pokémon mini (PKM)"));
	// a name may start with a space; the separator is one space
	assert(sync_list_line_name("-rw-r--r--              5 2026/10/15 12:00:00  lead.gba", n, sizeof(n), &dir));
	assert(!strcmp(n, " lead.gba"));
	assert(sync_list_line_name("lrwxrwxrwx              7 2026/10/15 12:00:00 link.gba -> real.gba", n, sizeof(n), &dir));
	assert(!strcmp(n, "link.gba") && !dir);
	assert(sync_list_line_name("-rw-r--r--          1,234 2026/10/15 12:00:00 Foo.gba\n", n, sizeof(n), &dir));
	assert(!strcmp(n, "Foo.gba"));
	const char* bad[] = {"drwxr-xr-x          4,096 2026/10/15 12:00:00 .", "", "receiving file list ... done",
						 "@ERROR: Unknown module 'x'", "drwxr-xr-x 4,096 2026/10/15", "rsync: connection refused", NULL};
	for (int i = 0; bad[i]; i++)
		assert(!sync_list_line_name(bad[i], n, sizeof(n), &dir));
	assert(!sync_list_line_name("-rw-r--r--              5 2026/10/15 12:00:00 a-long-name.gba", n, 8, &dir));
}

static void test_escape_pattern(void) {
	char b[64];
	assert(sync_escape_pattern("/GBA (GBA)/Foo.gba", b, sizeof(b)) > 0 && !strcmp(b, "/GBA (GBA)/Foo.gba"));
	// No wildcard: a backslash stays as it is (rsync reads it literally).
	assert(sync_escape_pattern("/X/a\\b.gba", b, sizeof(b)) > 0 && !strcmp(b, "/X/a\\b.gba"));
	assert(sync_escape_pattern("/X/Foo [!].gba", b, sizeof(b)) > 0 && !strcmp(b, "/X/Foo \\[!].gba"));
	assert(sync_escape_pattern("/X/What?*.gba", b, sizeof(b)) > 0 && !strcmp(b, "/X/What\\?\\*.gba"));
	assert(sync_escape_pattern("/X/a\\b [1].gba", b, sizeof(b)) > 0 && !strcmp(b, "/X/a\\\\b \\[1].gba"));
	assert(sync_escape_pattern("/X/[[[[", b, 8) == -1);
}

static void test_game_name(void) {
	assert(sync_is_game_name("Foo.gba") && sync_is_game_name("Final Fantasy VII (Disc 1)"));
	assert(!sync_is_game_name(".media") && !sync_is_game_name(".retroachievements") && !sync_is_game_name("._Foo.gba"));
	assert(!sync_is_game_name("map.txt") && !sync_is_game_name("Foo.gba.disabled") && !sync_is_game_name(""));
}

static void add_names(SyncNames* l, const char* const* items) {
	for (int i = 0; items[i]; i++)
		assert(sync_names_add(l, items[i]));
}

static void test_names(void) {
	SyncNames l = {0};
	add_names(&l, (const char*[]){"Foo.gba", "Pokémon.gb", NULL});
	assert(l.count == 2 && !strcmp(l.items[1], "Pokémon.gb"));
	assert(sync_names_has_ci(&l, "foo.GBA") && sync_names_has_ci(&l, "Pokémon.gb") && !sync_names_has_ci(&l, "Foo"));
	assert(sync_names_find(&l, "Pokémon.gb") == 1 && sync_names_find(&l, "foo.gba") == -1);
	sync_names_free(&l);
	assert(l.count == 0 && !l.items && !sync_names_has_ci(&l, "Foo.gba"));
}

static void test_sibling_excludes(void) {
	const char* gba = "Game Boy Advance (GBA)";
	SyncNames sender = {0}, recv_x = {0}, recv_sib = {0}, out = {0};
	add_names(&sender, (const char*[]){"Foo.gba", "Bar.gba", "Both.gba", "New.gba", "Q [!].gba", ".media", "map.txt",
									   "Disc Game", "line\nbreak.gba", NULL});
	add_names(&recv_x, (const char*[]){"both.gba", NULL});
	add_names(&recv_sib, (const char*[]){"foo.gba", "Bar.gba", "BOTH.gba", "q [!].gba", ".media", "map.txt", "disc game",
										 "line\nbreak.gba", NULL});
	assert(sync_sibling_excludes(gba, &sender, &recv_x, &recv_sib, &out) == 4);
	// In a sibling but not in X: left out. In X too (Both): synced as before.
	// Never hidden entries (art, map.txt), never a name that can't be a line.
	assert(!strcmp(out.items[0], "/Game Boy Advance (GBA)/Foo.gba"));
	assert(!strcmp(out.items[1], "/Game Boy Advance (GBA)/Bar.gba"));
	assert(!strcmp(out.items[2], "/Game Boy Advance (GBA)/Q \\[!].gba"));
	assert(!strcmp(out.items[3], "/Game Boy Advance (GBA)/Disc Game"));
	sync_names_free(&out);
	// No sibling entries: nothing excluded.
	SyncNames none = {0};
	assert(sync_sibling_excludes(gba, &sender, &recv_x, &none, &out) == 0 && out.count == 0);
	sync_names_free(&sender);
	sync_names_free(&recv_x);
	sync_names_free(&recv_sib);
}

static void test_phases(void) {
	// States and saves (1-4), BIOS (5-6) and collections (7) every sync, ROMs
	// (8-9) when on.
	assert(sync_phase_count(false) == 7 && sync_phase_count(true) == 9);
	for (int p = 0; p <= 10; p++) {
		assert(sync_is_bios_phase(p) == (p == SYNC_PHASE_BIOS_PUSH || p == SYNC_PHASE_BIOS_PULL));
		assert(sync_is_roms_phase(p) == (p == SYNC_PHASE_ROMS_PUSH || p == SYNC_PHASE_ROMS_PULL));
	}
	assert(SYNC_PHASE_BIOS_PUSH == 5 && SYNC_PHASE_BIOS_PULL == 6);
	assert(SYNC_PHASE_COLLECTIONS == 7);
	assert(SYNC_PHASE_ROMS_PUSH == 8 && SYNC_PHASE_ROMS_PULL == 9);
}

static void test_bios_skip(void) {
	// rsync's lines for an older peer without [bios] (3.2.0dev and 3.4.1).
	assert(sync_line_is_unknown_module("@ERROR: Unknown module 'bios'"));
	assert(sync_line_is_unknown_module("@ERROR: Unknown module 'bios'\n"));
	assert(!sync_line_is_unknown_module("@ERROR: chdir failed"));
	assert(!sync_line_is_unknown_module("@ERROR: access denied to bios from 10.99.0.2 (10.99.0.2)"));
	assert(!sync_line_is_unknown_module("rsync error: error starting client-server protocol (code 5) at main.c(1850) [sender=3.4.1]"));
	assert(!sync_line_is_unknown_module(""));

	// An older peer: exit 5 with the unknown-module message, the sync goes on.
	assert(sync_phase_skippable(SYNC_PHASE_BIOS_PUSH, 5, true));
	assert(sync_phase_skippable(SYNC_PHASE_BIOS_PULL, 5, true));
	assert(sync_phase_skippable(SYNC_PHASE_COLLECTIONS, 5, true));
	// Any other exit 5 (chdir failed, max connections) fails the phase.
	assert(!sync_phase_skippable(SYNC_PHASE_BIOS_PUSH, 5, false));
	assert(!sync_phase_skippable(SYNC_PHASE_BIOS_PULL, 5, false));
	assert(!sync_phase_skippable(SYNC_PHASE_COLLECTIONS, 5, false));
	// Any other failure of a BIOS phase fails the sync, as other phases do.
	assert(!sync_phase_skippable(SYNC_PHASE_BIOS_PUSH, 0, false));
	assert(!sync_phase_skippable(SYNC_PHASE_BIOS_PUSH, 0, true));
	assert(!sync_phase_skippable(SYNC_PHASE_BIOS_PUSH, 23, true));
	assert(!sync_phase_skippable(SYNC_PHASE_BIOS_PULL, 10, true));
	assert(!sync_phase_skippable(SYNC_PHASE_BIOS_PULL, -1, true));
	assert(!sync_phase_skippable(SYNC_PHASE_COLLECTIONS, 0, true));
	assert(!sync_phase_skippable(SYNC_PHASE_COLLECTIONS, 23, true));
	assert(!sync_phase_skippable(SYNC_PHASE_COLLECTIONS, -1, true));
	// Exit 5 elsewhere is a failure.
	for (int p = 1; p <= 4; p++)
		assert(!sync_phase_skippable(p, 5, true));
	assert(!sync_phase_skippable(SYNC_PHASE_ROMS_PUSH, 5, true));
	assert(!sync_phase_skippable(SYNC_PHASE_ROMS_PULL, 5, true));
}

// A list from a NULL-terminated array.
static SyncNames names_of(const char* const* items) {
	SyncNames l = {0};
	for (int i = 0; items[i]; i++)
		assert(sync_names_add(&l, items[i]));
	return l;
}

static bool names_are(const SyncNames* l, const char* const* items) {
	int n = 0;
	while (items[n])
		n++;
	if (l->count != n)
		return false;
	for (int i = 0; i < n; i++)
		if (strcmp(l->items[i], items[i]))
			return false;
	return true;
}

static SyncNames lines_of(const char* text) {
	SyncNames l = {0};
	assert(sync_text_lines(text, strlen(text), &l));
	return l;
}

static void test_collection_names(void) {
	assert(sync_is_collection_name("Favorites.txt"));
	assert(sync_is_collection_name("RPG (old).txt"));
	assert(sync_is_collection_name("Pok\xc3\xa9mon.txt"));
	// The alias map, hidden and junk files, other extensions.
	assert(!sync_is_collection_name("map.txt"));
	assert(!sync_is_collection_name("._Favorites.txt"));
	assert(!sync_is_collection_name(".hidden.txt"));
	assert(!sync_is_collection_name("Favorites.txt.tmp"));
	assert(!sync_is_collection_name("Favorites.txt.disabled"));
	assert(!sync_is_collection_name("Favorites.TXT"));
	assert(!sync_is_collection_name("Favorites"));
	assert(!sync_is_collection_name(".txt"));
	assert(!sync_is_collection_name(""));
	assert(!sync_is_collection_name("a/b.txt"));
}

static void test_text_lines(void) {
	SyncNames l = lines_of("/Roms/A (GBA)/a.gba\n/Roms/B (GB)/b.gb\n");
	assert(names_are(&l, (const char*[]){"/Roms/A (GBA)/a.gba", "/Roms/B (GB)/b.gb", NULL}));
	sync_names_free(&l);
	// No final newline.
	l = lines_of("a\nb");
	assert(names_are(&l, (const char*[]){"a", "b", NULL}));
	sync_names_free(&l);
	// \r\n files: the \r goes.
	l = lines_of("a\r\nb\r\n");
	assert(names_are(&l, (const char*[]){"a", "b", NULL}));
	sync_names_free(&l);
	// Only one trailing \r goes; blank lines stay (they're the file's own).
	l = lines_of("a\r\r\n\nb\n");
	assert(names_are(&l, (const char*[]){"a\r", "", "b", NULL}));
	sync_names_free(&l);
	// An empty file.
	l = lines_of("");
	assert(l.count == 0);
	sync_names_free(&l);
	l = lines_of("\n");
	assert(names_are(&l, (const char*[]){"", NULL}));
	sync_names_free(&l);
}

static void test_merge_lines(void) {
	SyncNames out = {0};
	// Order kept, the other side's lines this side lacks appended in its order.
	SyncNames mine = names_of((const char*[]){"/a", "/b", "/c", NULL});
	SyncNames theirs = names_of((const char*[]){"/d", "/b", "/e", NULL});
	assert(sync_merge_lines(&mine, &theirs, &out) == 2);
	assert(names_are(&out, (const char*[]){"/a", "/b", "/c", "/d", "/e", NULL}));
	sync_names_free(&out);
	// And the peer's version.
	assert(sync_merge_lines(&theirs, &mine, &out) == 2);
	assert(names_are(&out, (const char*[]){"/d", "/b", "/e", "/a", "/c", NULL}));
	sync_names_free(&out);
	sync_names_free(&mine);
	sync_names_free(&theirs);

	// Nothing new: unchanged (0 appended).
	mine = names_of((const char*[]){"/a", "/b", NULL});
	theirs = names_of((const char*[]){"/b", "/a", NULL});
	assert(sync_merge_lines(&mine, &theirs, &out) == 0);
	assert(names_are(&out, (const char*[]){"/a", "/b", NULL}));
	sync_names_free(&out);
	sync_names_free(&mine);
	sync_names_free(&theirs);

	// Duplicates: this side's own stay; a new line is added once. Blank lines
	// (empty or only spaces) are never added; this side's own stay.
	mine = names_of((const char*[]){"/a", "/a", "", NULL});
	theirs = names_of((const char*[]){"", "/x", "  ", "/x", "/a", "\t", "/y", NULL});
	assert(sync_merge_lines(&mine, &theirs, &out) == 2);
	assert(names_are(&out, (const char*[]){"/a", "/a", "", "/x", "/y", NULL}));
	sync_names_free(&out);
	sync_names_free(&mine);
	sync_names_free(&theirs);

	// \r\n against \n files: equal lines once the \r is trimmed.
	mine = lines_of("/a\r\n/b\r\n");
	theirs = lines_of("/b\n/c\n");
	assert(sync_merge_lines(&mine, &theirs, &out) == 1);
	assert(names_are(&out, (const char*[]){"/a", "/b", "/c", NULL}));
	sync_names_free(&out);
	sync_names_free(&mine);
	sync_names_free(&theirs);

	// A collection only on one side (or an empty file): the other's lines.
	SyncNames none = {0};
	theirs = names_of((const char*[]){"/a", "", "/b", "/a", NULL});
	assert(sync_merge_lines(&none, &theirs, &out) == 2);
	assert(names_are(&out, (const char*[]){"/a", "/b", NULL}));
	sync_names_free(&out);
	assert(sync_merge_lines(&theirs, &none, &out) == 0);
	assert(names_are(&out, (const char*[]){"/a", "", "/b", "/a", NULL}));
	sync_names_free(&out);
	assert(sync_merge_lines(&none, &none, &out) == 0 && out.count == 0);
	sync_names_free(&out);
	sync_names_free(&theirs);

	// Case and spaces matter: exact lines.
	mine = names_of((const char*[]){"/Roms/a.gba", NULL});
	theirs = names_of((const char*[]){"/roms/a.gba", "/Roms/a.gba ", NULL});
	assert(sync_merge_lines(&mine, &theirs, &out) == 2);
	sync_names_free(&out);
	sync_names_free(&mine);
	sync_names_free(&theirs);

	// Very long lines survive whole.
	static char longline[70000];
	memset(longline, 'x', sizeof(longline) - 1);
	longline[0] = '/';
	static char text[sizeof(longline) + 8];
	snprintf(text, sizeof(text), "%s\r\n/b\n", longline);
	theirs = lines_of(text);
	assert(theirs.count == 2 && strlen(theirs.items[0]) == sizeof(longline) - 1);
	mine = names_of((const char*[]){"/b", NULL});
	assert(sync_merge_lines(&mine, &theirs, &out) == 1);
	assert(out.count == 2 && !strcmp(out.items[1], longline));
	sync_names_free(&out);
	sync_names_free(&mine);
	sync_names_free(&theirs);
}

int main(void) {
	test_parse_flags();
	test_parse_rejects();
	test_format_roundtrip();
	test_roles();
	test_cable_net();
	test_filter_strings();
	test_console_display();
	test_siblings();
	test_list_line();
	test_escape_pattern();
	test_game_name();
	test_names();
	test_sibling_excludes();
	test_phases();
	test_bios_skip();
	test_collection_names();
	test_text_lines();
	test_merge_lines();
	printf("sync_proto: all tests passed\n");
	return 0;
}
