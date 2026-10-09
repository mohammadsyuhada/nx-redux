// Host test for netplay-wizard/wiz_caps.c: the optional capability token a
// launcher hands the wizard (--caps) and the wizard trades with the peer as a
// trailing "caps=<token>" HELLO field.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../netplay-wizard/wiz_caps.h"

static void expect_value(const char* caps, const char* key, const char* want) {
	char got[WIZ_CAPS_MAX];
	WizCaps_value(caps, key, got, sizeof(got));
	if (strcmp(got, want) != 0) {
		fprintf(stderr, "%s of '%s': expected '%s', got '%s'\n", key, caps ? caps : "(null)", want, got);
		assert(0);
	}
}

static void expect_label(const char* core, const char* tag, const char* want) {
	char got[64];
	WizCaps_coreLabel(core, tag, got, sizeof(got));
	if (strcmp(got, want) != 0) {
		fprintf(stderr, "label of '%s'/'%s': expected '%s', got '%s'\n", core ? core : "(null)",
				tag ? tag : "(null)", want, got);
		assert(0);
	}
}

static void expect_reason(const char* caps, const char* want) {
	char got[32]; // the REJECT reason buffer both sides use (%31s)
	WizCaps_coreReason(caps, got, sizeof(got));
	if (strcmp(got, want) != 0) {
		fprintf(stderr, "reason of '%s': expected '%s', got '%s'\n", caps, want, got);
		assert(0);
	}
}

static void expect_reason_label(const char* reason, const char* want) {
	char got[64];
	WizCaps_reasonLabel(reason, got, sizeof(got));
	if (strcmp(got, want) != 0) {
		fprintf(stderr, "reason label of '%s': expected '%s', got '%s'\n", reason, want, got);
		assert(0);
	}
}

static void expect_find(const char* line, const char* want) {
	char got[WIZ_CAPS_MAX];
	WizCaps_find(line, got, sizeof(got));
	if (strcmp(got, want) != 0) {
		fprintf(stderr, "'%s': expected '%s', got '%s'\n", line, want, got);
		assert(0);
	}
}

int main(void) {
	// tokens: short, wire-safe, no spaces
	assert(WizCaps_isValid("dcbios=0123456789ab"));
	assert(WizCaps_isValid("dcbios=none"));
	assert(WizCaps_isValid("a=1,b=2"));
	assert(WizCaps_isValid("x.y_z-1"));
	assert(!WizCaps_isValid(""));
	assert(!WizCaps_isValid(NULL));
	assert(!WizCaps_isValid("has space"));
	assert(!WizCaps_isValid("semi;colon"));
	assert(!WizCaps_isValid("quote'"));
	assert(!WizCaps_isValid("dollar$x"));
	char big[WIZ_CAPS_MAX + 1];
	memset(big, 'a', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	assert(!WizCaps_isValid(big)); // one over the limit

	// found anywhere after the fixed HELLO fields, in either direction
	expect_find("HELLO 1 Soulcalibur\x1f(USA) client caps=dcbios=abc", "dcbios=abc");
	expect_find("HELLO 1 game client any caps=dcbios=none", "dcbios=none");
	expect_find("HELLO 1 game host 2 caps=dcbios=abc", "dcbios=abc");
	// absent (an older wizard, or a pak that sends none)
	expect_find("HELLO 1 game client", "");
	expect_find("HELLO 1 game host 2", "");
	expect_find("HELLO 1 game client any", "");
	// a game token that merely contains "caps=" is not the field
	expect_find("HELLO 1 caps=x client", "");
	// an invalid token is dropped, not trusted
	expect_find("HELLO 1 game client caps=bad;rm", "");
	expect_find("HELLO 1 game client caps=", "");
	// trailing newline from the wire
	expect_find("HELLO 1 game host 2 caps=dcbios=abc\n", "dcbios=abc");
	expect_find(NULL, "");

	// the line a sender appends (empty when it has no caps)
	char field[WIZ_CAPS_MAX + 8];
	WizCaps_field("dcbios=abc", field, sizeof(field));
	assert(strcmp(field, " caps=dcbios=abc") == 0);
	WizCaps_field(NULL, field, sizeof(field));
	assert(strcmp(field, "") == 0);
	WizCaps_field("bad token", field, sizeof(field));
	assert(strcmp(field, "") == 0);

	// one element of a comma-separated token, by key
	expect_value("core=picodrive,tag=MD", "core", "picodrive");
	expect_value("core=picodrive,tag=MD", "tag", "MD");
	expect_value("tag=GPGX,core=genesis_plus_gx", "core", "genesis_plus_gx");
	expect_value("dcbios=abc", "core", "");
	expect_value("dcbios=abc", "tag", "");
	expect_value("xcore=gpsp", "core", ""); // a different key that merely ends in it
	expect_value("core=", "core", "");
	expect_value("core", "core", "");
	expect_value("", "core", "");
	expect_value(NULL, "core", "");
	char tiny[3];
	WizCaps_value("core=gpsp", "core", tiny, sizeof(tiny));
	assert(strcmp(tiny, "gp") == 0); // truncated, still terminated

	// refused only when BOTH sides name a core and the two differ
	assert(WizCaps_coreMismatch("core=gpsp,tag=GBA", "core=mgba,tag=MGBA"));
	assert(WizCaps_coreMismatch("core=picodrive,tag=MD", "core=genesis_plus_gx,tag=GPGX"));
	assert(WizCaps_coreMismatch("core=snes9x,tag=SFC", "tag=SUPA,core=mednafen_supafaust"));
	assert(!WizCaps_coreMismatch("core=gpsp,tag=GBA", "core=gpsp,tag=GBA"));
	// the same core in another folder is fine (the game check covers the rest)
	assert(!WizCaps_coreMismatch("core=picodrive,tag=MD", "core=picodrive,tag=GG"));
	assert(!WizCaps_coreMismatch("core=mgba,tag=MGBA", "")); // an older build sends none
	assert(!WizCaps_coreMismatch("", "core=mgba,tag=MGBA"));
	assert(!WizCaps_coreMismatch("core=mgba", NULL));
	assert(!WizCaps_coreMismatch(NULL, NULL));
	assert(!WizCaps_coreMismatch("tag=MD", "tag=GPGX")); // tags alone never refuse
	assert(!WizCaps_coreMismatch("dcbios=a", "dcbios=b"));

	// what the player is told the other device runs
	assert(strcmp(WizCaps_coreName("gpsp"), "gpSP") == 0);
	assert(strcmp(WizCaps_coreName("mgba"), "mGBA") == 0);
	assert(strcmp(WizCaps_coreName("picodrive"), "PicoDrive") == 0);
	assert(strcmp(WizCaps_coreName("genesis_plus_gx"), "Genesis Plus GX") == 0);
	assert(strcmp(WizCaps_coreName("snes9x"), "Snes9x") == 0);
	assert(strcmp(WizCaps_coreName("mednafen_supafaust"), "Supafaust") == 0);
	assert(strcmp(WizCaps_coreName("pcsx_rearmed"), "PCSX ReARMed") == 0);
	assert(strcmp(WizCaps_coreName("swanstation"), "SwanStation") == 0);
	assert(strcmp(WizCaps_coreName("fbneo"), "FBNeo") == 0);
	assert(strcmp(WizCaps_coreName("fceumm"), "FCEUmm") == 0);
	assert(strcmp(WizCaps_coreName("vba_next"), "vba_next") == 0);
	assert(strcmp(WizCaps_coreName(NULL), "") == 0);
	expect_label("gpsp", "GBA", "gpSP (GBA folder)");
	expect_label("genesis_plus_gx", "GPGX", "Genesis Plus GX (GPGX folder)");
	expect_label("swanstation", "", "SwanStation");
	expect_label("swanstation", NULL, "SwanStation");
	expect_label("vba_next", "VBA", "vba_next (VBA folder)");

	// the host's core and folder, packed into one REJECT reason token
	expect_reason("core=gpsp,tag=GBA", "core-gpsp.GBA");
	expect_reason("tag=SUPA,core=mednafen_supafaust", "core-mednafen_supafaust.SUPA");
	expect_reason("core=swanstation", "core-swanstation");
	// a folder name too long to fit drops to the core alone, then to bare "core"
	expect_reason("core=picodrive,tag=AVERYLONGCUSTOMFOLDERNAME", "core-picodrive");
	expect_reason("core=an_extremely_long_core_name_x", "core");
	expect_reason("dcbios=abc", "core");
	// and unpacked on the joiner
	expect_reason_label("core-gpsp.GBA", "gpSP (GBA folder)");
	expect_reason_label("core-mednafen_supafaust.SUPA", "Supafaust (SUPA folder)");
	expect_reason_label("core-swanstation", "SwanStation");
	expect_reason_label("core", "");
	expect_reason_label("version", "");

	printf("test_wiz_caps: OK\n");
	return 0;
}
