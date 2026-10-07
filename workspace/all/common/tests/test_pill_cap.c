// Host test for ui_pill_cap.h: the anti-aliased end cap of a pill drawn at a height no asset sheet has.
#include "../ui_pill_cap.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
	int h = 76, r = h / 2;
	// the left cap's flat inner edge at mid height is solid; its outer corners are empty
	assert(PillCap_coverage(r, h, r - 1, h / 2, true) == 255);
	assert(PillCap_coverage(r, h, 0, 0, true) == 0);
	assert(PillCap_coverage(r, h, 0, h - 1, true) == 0);
	// the curve is anti-aliased: the leftmost column has partly covered pixels where the circle crosses it
	int partial = 0;
	for (int y = 0; y < h; y++) {
		int c = PillCap_coverage(r, h, 0, y, true);
		partial += c > 0 && c < 255;
	}
	assert(partial > 0);
	// the right cap mirrors the left one
	for (int y = 0; y < h; y++)
		for (int x = 0; x < r; x++)
			assert(PillCap_coverage(r, h, x, y, true) == PillCap_coverage(r, h, r - 1 - x, y, false));
	// top and bottom mirror each other
	for (int x = 0; x < r; x++)
		assert(PillCap_coverage(r, h, x, 3, true) == PillCap_coverage(r, h, x, h - 1 - 3, true));
	printf("test_pill_cap: all passed\n");
	return 0;
}
