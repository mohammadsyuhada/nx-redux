// Host test for nextui/area_scale.c: area-average shrinking with alpha-weighted colour.
#include "../../nextui/area_scale.h"
#include <assert.h>
#include <stdio.h>

#define ARGB(a, r, g, b) ((uint32_t)(a) << 24 | (uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define A(p) ((p) >> 24)
#define R(p) (((p) >> 16) & 0xFF)

static int near8(unsigned v, unsigned want) {
	return v + 1 >= want && v <= want + 1;
}

// identity: same size copies every pixel
static void identity(void) {
	uint32_t src[4] = {ARGB(255, 10, 20, 30), ARGB(128, 200, 100, 50), 0, ARGB(1, 255, 255, 255)};
	uint32_t dst[4];
	assert(AreaScale_argb(src, 2, 2, 2, dst, 2, 2, 2) == 0);
	assert(dst[0] == src[0] && dst[1] == src[1] && dst[2] == 0 && dst[3] == src[3]);
}

// halving averages each 2x2 block: every source pixel counts (nearest would keep one in four)
static void halving(void) {
	uint32_t src[16];
	for (int i = 0; i < 16; i++)
		src[i] = ARGB(255, (i % 4) * 60, 0, 0);
	uint32_t dst[4];
	assert(AreaScale_argb(src, 4, 4, 4, dst, 2, 2, 2) == 0);
	assert(A(dst[0]) == 255 && near8(R(dst[0]), 30) && near8(R(dst[1]), 150));
}

// a white edge pixel next to transparent black: alpha halves, the colour stays white (no dark fringe)
static void no_dark_fringe(void) {
	uint32_t src[2] = {ARGB(255, 255, 255, 255), ARGB(0, 0, 0, 0)};
	uint32_t dst[1];
	assert(AreaScale_argb(src, 2, 1, 2, dst, 1, 1, 1) == 0);
	assert(near8(A(dst[0]), 128) && R(dst[0]) == 255);
	uint32_t clear[2] = {0, 0};
	assert(AreaScale_argb(clear, 2, 1, 2, dst, 1, 1, 1) == 0 && dst[0] == 0);
}

// a non-integer shrink (3 -> 2) splits the middle pixel between both outputs
static void fractional(void) {
	uint32_t src[3] = {ARGB(255, 0, 0, 0), ARGB(255, 90, 0, 0), ARGB(255, 180, 0, 0)};
	uint32_t dst[2];
	assert(AreaScale_argb(src, 3, 1, 3, dst, 2, 1, 2) == 0);
	assert(near8(R(dst[0]), 30) && near8(R(dst[1]), 150) && A(dst[0]) == 255 && A(dst[1]) == 255);
}

// growing and padded pitches stay in bounds; bad sizes are refused
static void growing_and_pitch(void) {
	uint32_t src[6] = {ARGB(255, 100, 0, 0), ARGB(255, 200, 0, 0), 0xDEADBEEF, 0, 0, 0};
	uint32_t dst[12];
	assert(AreaScale_argb(src, 2, 1, 3, dst, 4, 2, 6) == 0);
	assert(R(dst[0]) == 100 && R(dst[3]) == 200 && R(dst[6]) == 100 && A(dst[9]) == 255);
	assert(AreaScale_argb(src, 0, 1, 3, dst, 4, 2, 6) == -1);
	assert(AreaScale_argb(src, 2, 1, 3, dst, 0, 2, 6) == -1);
}

int main(void) {
	identity();
	halving();
	no_dark_fringe();
	fractional();
	growing_and_pitch();
	printf("test_area_scale: all passed\n");
	return 0;
}
