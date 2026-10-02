// Host test for nextui/area_scale.c: area-average shrinking with alpha-weighted colour.
#include "../../nextui/area_scale.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

// The scalar bilinear homeart.c's cropFill used before AreaScale_bilinearCover: per pixel, per channel, horizontal
// blends then the vertical one.
static uint32_t oldLerp(uint32_t a, uint32_t b, int t) {
	uint32_t out = 0;
	for (int sh = 0; sh < 32; sh += 8) {
		int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
		out |= (uint32_t)((ca * (256 - t) + cb * t + 128) >> 8) << sh;
	}
	return out;
}

static void oldBilinear(const uint32_t* sp, int sw, int sh, int spitch, uint32_t* dp, int w, int h, int dpitch, float s,
						float ox, float oy) {
	int maxx = sw - 1, maxy = sh - 1;
	for (int y = 0; y < h; y++) {
		float fy = (y + 0.5f + oy) / s - 0.5f;
		if (fy < 0)
			fy = 0;
		if (fy > maxy)
			fy = maxy;
		int y0 = (int)fy, y1 = y0 < maxy ? y0 + 1 : y0;
		int ty = (int)((fy - y0) * 256);
		const uint32_t* r0 = sp + y0 * spitch;
		const uint32_t* r1 = sp + y1 * spitch;
		for (int x = 0; x < w; x++) {
			float fx = (x + 0.5f + ox) / s - 0.5f;
			if (fx < 0)
				fx = 0;
			if (fx > maxx)
				fx = maxx;
			int x0 = (int)fx, x1 = x0 < maxx ? x0 + 1 : x0;
			int tx = (int)((fx - x0) * 256);
			dp[y * dpitch + x] = oldLerp(oldLerp(r0[x0], r0[x1], tx), oldLerp(r1[x0], r1[x1], tx), ty);
		}
	}
}

// a gradient in every channel, alpha included, plus a hard edge and noise so both blends see real differences
static uint32_t* gradient(int w, int h, int pitch) {
	uint32_t* px = malloc(sizeof(uint32_t) * (size_t)pitch * h);
	unsigned seed = 12345;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < pitch; x++) {
			seed = seed * 1103515245u + 12345u;
			unsigned n = (seed >> 16) & 0x1F;
			unsigned a = 255 - (y * 200 / h), r = x * 255 / pitch, g = (x + y) * 255 / (pitch + h);
			unsigned b = (x > w / 2 ? 230 : 20) + (n > 15 ? n - 15 : 0);
			px[(size_t)y * pitch + x] = ARGB(a, r, g, b);
		}
	return px;
}

static int maxChannelDiff(const uint32_t* a, const uint32_t* b, int w, int h, int pitch) {
	int worst = 0;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			for (int sh = 0; sh < 32; sh += 8) {
				int d = (int)((a[(size_t)y * pitch + x] >> sh) & 0xFF) - (int)((b[(size_t)y * pitch + x] >> sh) & 0xFF);
				if (d < 0)
					d = -d;
				if (d > worst)
					worst = d;
			}
	return worst;
}

// the new kernel matches the old scalar within 2 per channel: upscale, slight shrink, crop offsets, padded pitches
static void bilinear_matches_old(void) {
	struct {
		int sw, sh, dw, dh;
		float s, ox, oy;
	} cases[] = {
		{64, 36, 160, 90, 2.5f, 0.0f, 0.0f},
		{100, 80, 61, 49, 0.61f, 0.0f, 0.0f},
		{40, 60, 90, 70, 2.25f, 0.0f, 33.0f},
		{37, 23, 37, 23, 1.0f, 0.0f, 0.0f},
		{320, 180, 256, 160, 0.9f, 16.0f, 2.0f},
		{1, 1, 5, 3, 5.0f, 0.0f, 0.0f},
		{50, 2, 70, 9, 4.5f, 70.0f, 0.0f},
	};
	for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
		int spitch = cases[c].sw + 3, dpitch = cases[c].dw + 5;
		uint32_t* src = gradient(cases[c].sw, cases[c].sh, spitch);
		uint32_t* want = calloc((size_t)dpitch * cases[c].dh, 4);
		uint32_t* got = calloc((size_t)dpitch * cases[c].dh, 4);
		oldBilinear(src, cases[c].sw, cases[c].sh, spitch, want, cases[c].dw, cases[c].dh, dpitch, cases[c].s, cases[c].ox,
					cases[c].oy);
		assert(AreaScale_bilinearCover(src, cases[c].sw, cases[c].sh, spitch, got, cases[c].dw, cases[c].dh, dpitch,
									   cases[c].s, cases[c].ox, cases[c].oy) == 0);
		assert(maxChannelDiff(want, got, cases[c].dw, cases[c].dh, dpitch) <= 2);
		free(src);
		free(want);
		free(got);
	}
	uint32_t one = 0, out = 0;
	assert(AreaScale_bilinearCover(&one, 1, 1, 1, &out, 1, 1, 1, 0.0f, 0, 0) == -1);
	assert(AreaScale_bilinearCover(&one, 0, 1, 1, &out, 1, 1, 1, 1.0f, 0, 0) == -1);
}

static double now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

// --bench: old scalar vs new kernel filling 1280x720 (the Backdrop) from a 640x360 and a 1600x900 screenshot
static void bench(void) {
	int sizes[2][2] = {{640, 360}, {1600, 900}};
	uint32_t* dst = malloc(sizeof(uint32_t) * 1280 * 720);
	for (int k = 0; k < 2; k++) {
		int sw = sizes[k][0], sh = sizes[k][1];
		uint32_t* src = gradient(sw, sh, sw);
		float s = 1280.0f / sw;
		int reps = 20;
		double t0 = now();
		for (int i = 0; i < reps; i++)
			oldBilinear(src, sw, sh, sw, dst, 1280, 720, 1280, s, 0, 0);
		double t1 = now();
		for (int i = 0; i < reps; i++)
			AreaScale_bilinearCover(src, sw, sh, sw, dst, 1280, 720, 1280, s, 0, 0);
		double t2 = now();
		printf("bench %dx%d -> 1280x720: old %.2f ms, new %.2f ms (%.1fx)\n", sw, sh, (t1 - t0) * 1000 / reps,
			   (t2 - t1) * 1000 / reps, (t1 - t0) / (t2 - t1));
		free(src);
	}
	free(dst);
}

int main(int argc, char** argv) {
	if (argc > 1 && !strcmp(argv[1], "--bench")) {
		bench();
		return 0;
	}
	identity();
	halving();
	no_dark_fringe();
	fractional();
	growing_and_pitch();
	bilinear_matches_old();
	printf("test_area_scale: all passed\n");
	return 0;
}
