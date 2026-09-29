// Host test for netplay-wizard/wiz_caps.c: the optional capability token a
// launcher hands the wizard (--caps) and the wizard trades with the peer as a
// trailing "caps=<token>" HELLO field.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../netplay-wizard/wiz_caps.h"

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

	printf("test_wiz_caps: OK\n");
	return 0;
}
