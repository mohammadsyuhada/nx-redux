// Host test for ma_core_progress.c: core-reported progress text (e.g. GLideN64
// converting a hi-res texture pack) split into title/detail, active while fresh.
// cc -std=gnu99 -I. tests/test_core_progress.c ma_core_progress.c -lpthread -o /tmp/t_cp && /tmp/t_cp
#include <stdio.h>
#include <string.h>
#include "ma_core_progress.h"

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

static unsigned long fake_now = 1000;
static unsigned long fake_clock(void) {
	return fake_now;
}

int main(void) {
	char title[64], detail[128];
	CoreProgress_setClock(fake_clock);

	CHECK(!CoreProgress_active(title, sizeof(title), detail, sizeof(detail)), "nothing reported: inactive");

	CoreProgress_set("Processing hi-res textures (first time only)...\n[12] total mem:3.5mb - a.png");
	CHECK(CoreProgress_active(title, sizeof(title), detail, sizeof(detail)), "fresh message: active");
	CHECK(strcmp(title, "Processing hi-res textures (first time only)...") == 0, "title is the first line");
	CHECK(strcmp(detail, "[12] total mem:3.5mb - a.png") == 0, "detail is the rest");

	CoreProgress_set("CREATING FILE INDEX. PLEASE WAIT...");
	CoreProgress_active(title, sizeof(title), detail, sizeof(detail));
	CHECK(strcmp(title, "CREATING FILE INDEX. PLEASE WAIT...") == 0 && detail[0] == '\0', "single line: title only");

	fake_now += 599;
	CHECK(CoreProgress_active(title, sizeof(title), detail, sizeof(detail)), "599 ms later: still active");
	fake_now += 2;
	CHECK(!CoreProgress_active(title, sizeof(title), detail, sizeof(detail)), "after 600 ms of silence: inactive");

	CoreProgress_set("x");
	CoreProgress_set("");
	CHECK(!CoreProgress_active(title, sizeof(title), detail, sizeof(detail)), "empty message clears");

	char tiny[8];
	CoreProgress_set("A very long title line\ndetail");
	CoreProgress_active(tiny, sizeof(tiny), detail, sizeof(detail));
	CHECK(strlen(tiny) == sizeof(tiny) - 1, "title clipped to the buffer");

	printf(fails ? "FAILED (%d failures)\n" : "ALL PASS (0 failures)\n", fails);
	return fails != 0;
}
