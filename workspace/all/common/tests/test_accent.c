#include "../ui/ui_accent.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int near(double a, double b, double eps) {
	return fabs(a - b) <= eps;
}

// Expected values from the WCAG 2.x formula: L = .2126 R + .7152 G + .0722 B over the linearised channels,
// contrast = (L1 + .05) / (L2 + .05); the ink is whichever of black / white contrasts more.
static void wcag_numbers(void) {
	assert(near(UI_relativeLuminance(0xFFFFFF), 1.0, 1e-9));
	assert(near(UI_relativeLuminance(0x000000), 0.0, 1e-9));
	assert(near(UI_contrastRatio(0xFFFFFF, 0x000000), 21.0, 1e-9));
	assert(near(UI_contrastRatio(0x000000, 0xFFFFFF), 21.0, 1e-9));
	assert(near(UI_relativeLuminance(0xE60012), 0.1687, 1e-4));
	assert(near(UI_relativeLuminance(0x8B5CF6), 0.1980, 1e-4));
	// NX red: white 4.80 beats black 4.37
	assert(near(UI_contrastRatio(0xE60012, 0xFFFFFF), 4.80, 0.01));
	assert(near(UI_contrastRatio(0xE60012, 0x000000), 4.37, 0.01));
	// violet: black 4.96 beats white 4.23
	assert(near(UI_contrastRatio(0x8B5CF6, 0x000000), 4.96, 0.01));
	assert(near(UI_contrastRatio(0x8B5CF6, 0xFFFFFF), 4.23, 0.01));
}

static void ink_choice(void) {
	assert(UI_onAccentFor(0xFFFFFF) == 0x000000);				// white -> black
	assert(UI_onAccentFor(0x000000) == 0xFFFFFF);				// black -> white
	assert(UI_onAccentFor(0xE60012) == 0xFFFFFF);				// NX red -> white
	assert(UI_onAccentFor(0xFFC107) == 0x000000);				// amber -> black
	assert(UI_onAccentFor(0xA4E400) == 0x000000);				// lime -> black
	assert(UI_onAccentFor(0x2FA8FF) == 0x000000);				// sky -> black
	assert(UI_onAccentFor(0x8B5CF6) == 0x000000);				// violet -> black (WCAG: 4.96 vs 4.23)
	assert(UI_onAccentFor(0xFF000000u | 0xFFFFFF) == 0x000000); // a stray alpha byte is ignored
}

static void override_default_and_set(void) {
	uint32_t rgb = 0x123456;
	// without -DNX_ACCENT the accent follows the theme (Color 1 / Color 5): no override, *rgb untouched
	assert(!UI_accentOverride(&rgb));
	assert(rgb == 0x123456);
	assert(!UI_accentOverride(NULL));
	UI_setAccent(0xE60012);
	assert(UI_accentOverride(&rgb));
	assert(rgb == 0xE60012);
	assert(UI_onAccentFor(rgb) == 0xFFFFFF);
	UI_setAccent(0x12FFFFFF); // masked to 24 bits
	assert(UI_accentOverride(&rgb));
	assert(rgb == 0xFFFFFF);
	assert(UI_onAccentFor(rgb) == 0x000000);
}

int main(void) {
	wcag_numbers();
	ink_choice();
	override_default_and_set();
	printf("test_accent: all passed\n");
	return 0;
}
