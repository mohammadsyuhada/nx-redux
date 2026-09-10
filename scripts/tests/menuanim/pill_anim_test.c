// Host unit test: the shared selection-pill glide (ui_list.c) must honour the
// "Show menu animations" setting. With the setting off, a caller asking for an
// animated retarget gets a snap instead — no intermediate frames, not active.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "api.h"
#include "ui_list.h"

// Data symbols ui_list.c references at load time; the functions it calls are
// never reached by these tests and stay unresolved (see the runner script).
uint32_t RGB_WHITE, THEME_COLOR1, THEME_COLOR2, THEME_COLOR4_255, THEME_COLOR5_255;
GFX_Fonts font;

// Stand-in for config.c: the test flips this to model the user's setting.
static bool g_menu_animations = true;
bool CFG_getMenuAnimations(void) {
	return g_menu_animations;
}

static int failures = 0;
#define CHECK(cond, ...)                                \
	do {                                                \
		if (!(cond)) {                                  \
			failures++;                                 \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__);                        \
			printf("\n");                               \
		}                                               \
	} while (0)

static void test_pill_glides_when_enabled(void) {
	g_menu_animations = true;
	PillAnimState s = {.current_y = 100, .target_y = 100, .current_w = 200, .target_w = 200};
	UI_pillAnimSetTarget(&s, 160, 240, true);
	CHECK(UI_pillAnimIsActive(&s), "enabled: retarget should start a glide");
	int y = UI_pillAnimTick(&s);
	CHECK(y != 160, "enabled: first tick must not already sit on the target (got %d)", y);
}

static void test_pill_snaps_when_disabled(void) {
	g_menu_animations = false;
	PillAnimState s = {.current_y = 100, .target_y = 100, .current_w = 200, .target_w = 200};
	UI_pillAnimSetTarget(&s, 160, 240, true);
	CHECK(!UI_pillAnimIsActive(&s), "disabled: retarget must not start a glide");
	int y = UI_pillAnimTick(&s);
	CHECK(y == 160, "disabled: pill must sit on the target immediately (got %d)", y);
	CHECK(s.current_w == 240, "disabled: width must snap too (got %d)", s.current_w);
}

static void test_list_glide_snaps_when_disabled(void) {
	g_menu_animations = false;
	ListGlide g = {0};
	int dummy_list = 0;
	// First draw seeds the list identity (snap regardless of setting).
	UI_listGlideDrawAtY(&g, NULL, &dummy_list, 100, 0, 600, 60, 0, true);
	// Same list, selection moved one row: with animations off this must snap.
	ListGlideFrame f = UI_listGlideDrawAtY(&g, NULL, &dummy_list, 160, 0, 600, 60, 0, true);
	CHECK(!f.animating, "disabled: ListGlide must not report animating");
	CHECK(f.pill_y == 160, "disabled: ListGlide pill must be on target (got %d)", f.pill_y);
	CHECK(!UI_listGlideActive(&g), "disabled: ListGlide must not stay active");
}

int main(void) {
	test_pill_glides_when_enabled();
	test_pill_snaps_when_disabled();
	test_list_glide_snaps_when_disabled();
	if (failures) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("pill animation setting: all checks passed\n");
	return 0;
}
