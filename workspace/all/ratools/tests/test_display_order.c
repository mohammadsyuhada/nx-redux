// Host test for RAT_firstLocked: "Next" follows the server's list order, not the achievement ID.
// cc -I workspace/all/ratools workspace/all/ratools/tests/test_display_order.c \
//    -o /tmp/test_display_order && /tmp/test_display_order
#include <stdio.h>
#include <string.h>
#include "ratools_data.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

// Tetris (Game Boy) core set as the server lists it: "Challenge of the Best" has the lowest ID but
// comes 12th; the score ladder (Newbie..Master) comes first.
static const struct {
	uint32_t id;
	const char* title;
} tetris[] = {
	{46824, "Tetris Newbie"},
	{46825, "Tetris Regular"},
	{46837, "Tetris Champion"},
	{46838, "Tetris Master"},
	{46828, "Line 'Em Up"},
	{46829, "Line 'Em Up Extreme"},
	{46830, "Type B Newbie"},
	{46831, "Type B Regular"},
	{46832, "Type B Champion"},
	{46833, "Type B Master"},
	{46834, "Type B God"},
	{46819, "Challenge of the Best"},
	{46820, "Challenge of the Gods"},
};
#define TETRIS_COUNT ((int)(sizeof(tetris) / sizeof(tetris[0])))

static void load_tetris(RAT_Achievement* achs) {
	memset(achs, 0, sizeof(RAT_Achievement) * TETRIS_COUNT);
	for (int i = 0; i < TETRIS_COUNT; i++) {
		achs[i].id = tetris[i].id;
		achs[i].order = i; // RAT_loadAchievements numbers them in list order
		snprintf(achs[i].title, sizeof(achs[i].title), "%s", tetris[i].title);
	}
}

static RAT_Achievement* find(RAT_Achievement* achs, const char* title) {
	for (int i = 0; i < TETRIS_COUNT; i++)
		if (!strcmp(achs[i].title, title))
			return &achs[i];
	return NULL;
}

int main(void) {
	RAT_Achievement achs[TETRIS_COUNT];
	const RAT_Achievement* next;

	load_tetris(achs);
	find(achs, "Line 'Em Up")->state = RAT_ACH_UNLOCKED;
	next = RAT_firstLocked(achs, TETRIS_COUNT);
	CHECK(next && !strcmp(next->title, "Tetris Newbie"), "Line 'Em Up done: next is Tetris Newbie, not lowest ID");

	find(achs, "Tetris Newbie")->state = RAT_ACH_PENDING;
	next = RAT_firstLocked(achs, TETRIS_COUNT);
	CHECK(next && !strcmp(next->title, "Tetris Regular"), "pending unlock counts as unlocked");

	// a shuffled array (e.g. after a sort) still picks by order
	load_tetris(achs);
	RAT_Achievement tmp = achs[0];
	achs[0] = achs[11];
	achs[11] = tmp;
	next = RAT_firstLocked(achs, TETRIS_COUNT);
	CHECK(next && !strcmp(next->title, "Tetris Newbie"), "picks by order, not array position");

	for (int i = 0; i < TETRIS_COUNT; i++)
		achs[i].state = RAT_ACH_UNLOCKED;
	CHECK(RAT_firstLocked(achs, TETRIS_COUNT) == NULL, "all unlocked: no next");
	CHECK(RAT_firstLocked(achs, 0) == NULL, "empty set: no next");

	printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
	return fails ? 1 : 0;
}
