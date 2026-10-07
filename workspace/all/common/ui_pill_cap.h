// A pill's anti-aliased end cap at any height: what the pill asset's caps can't give once the pill is drawn shorter
// than the sheet's (a list with more rows than its PILL_SIZE pitch fits). Pure and SDL-free; host-tested by
// tests/test_pill_cap.c.
#ifndef UI_PILL_CAP_H
#define UI_PILL_CAP_H

#include <stdbool.h>
#include <stdint.h>

// The coverage (0-255) of pixel (x, y) in an r-wide, h-tall cap: the half of a circle of radius h/2 whose centre sits on
// the cap's inner edge (the left cap's right edge, the right cap's left edge), sampled 4x4 per pixel.
static inline uint8_t PillCap_coverage(int r, int h, int x, int y, bool left) {
	float cx = left ? (float)r : 0.0f, cy = h / 2.0f, rr = (h / 2.0f) * (h / 2.0f);
	int in = 0;
	for (int j = 0; j < 4; j++)
		for (int i = 0; i < 4; i++) {
			float dx = x + (i + 0.5f) / 4.0f - cx, dy = y + (j + 0.5f) / 4.0f - cy;
			if (dx * dx + dy * dy <= rr)
				in++;
		}
	return (uint8_t)(in * 255 / 16);
}

#endif
