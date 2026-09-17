#include <stdio.h>
#include <string.h>
#include "ma_cheat_match.h"

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

static int has(const int* a, int n, int v) {
	for (int i = 0; i < n; i++)
		if (a[i] == v)
			return 1;
	return 0;
}

int main(void) {
	int idx[16];

	// 1. exact single match qualifies and is selected
	{
		const char* c[] = {"Advance Wars (USA, Europe) (Code Breaker).cht"};
		int n = CheatMatch_select("Advance Wars", "Advance Wars (USA, Europe)", c, 1, idx, 16);
		CHECK(n == 1 && idx[0] == 0, "exact single match selected");
	}

	// 2. ambiguity guard: different stems must NOT match
	{
		const char* c[] = {"Super Mario World (USA).cht", "Super Mario Bros (USA).cht"};
		int n = CheatMatch_select("Super Mario", "Super Mario (USA)", c, 2, idx, 16);
		CHECK(n == 0, "different-stem candidates rejected");
	}

	// 3. region preference: USA ROM prefers the USA file over the Japan file
	{
		const char* c[] = {"Sonic (Japan).cht", "Sonic (USA).cht"};
		int n = CheatMatch_select("Sonic", "Sonic (USA)", c, 2, idx, 16);
		CHECK(n == 1 && idx[0] == 1, "region match preferred");
	}

	// 4. variant merge: same game+region, different cheat source -> both selected
	{
		const char* c[] = {"Contra (USA) (Code Breaker).cht", "Contra (USA) (GameShark).cht"};
		int n = CheatMatch_select("Contra", "Contra (USA)", c, 2, idx, 16);
		CHECK(n == 2 && has(idx, n, 0) && has(idx, n, 1), "same-region variants merged");
	}

	// 5. plain file preferred when ROM has no region tags
	{
		const char* c[] = {"Zelda.cht", "Zelda (USA).cht"};
		int n = CheatMatch_select("Zelda", "Zelda", c, 2, idx, 16);
		CHECK(n == 1 && idx[0] == 0, "plain file preferred for region-less ROM");
	}

	// 6. source label extraction
	{
		char lbl[64];
		CheatMatch_sourceLabel("Contra (USA) (GameShark).cht", lbl, sizeof(lbl));
		CHECK(strcmp(lbl, "GameShark") == 0, "source label = GameShark");
		CheatMatch_sourceLabel("Zelda (USA).cht", lbl, sizeof(lbl));
		CHECK(lbl[0] == '\0', "no source label when only region tags");
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
