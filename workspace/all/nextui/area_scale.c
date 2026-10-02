// area_scale.c — alpha-aware area-average resampling of ARGB8888 pixels. SDL-free.
#include "area_scale.h"
#include <stdlib.h>

// The source span an output index covers, as source indices [*i0, *i1) with the first and last ones' overlap.
typedef struct {
	int i0, i1;
	float w0, w1; // overlap of the first and the last covered source index (1 for the ones in between)
} Span;

static Span spanOf(int d, float ratio, int n) {
	float a = d * ratio, b = (d + 1) * ratio;
	Span s;
	s.i0 = (int)a;
	s.i1 = (int)b;
	if ((float)s.i1 < b)
		s.i1++;
	if (s.i1 > n)
		s.i1 = n;
	if (s.i0 >= s.i1)
		s.i0 = s.i1 - 1;
	if (s.i1 - s.i0 == 1) {
		s.w0 = s.w1 = b - a;
	} else {
		s.w0 = (float)(s.i0 + 1) - a;
		s.w1 = b - (float)(s.i1 - 1);
	}
	return s;
}

static float weightAt(const Span* s, int i) {
	return i == s->i0 ? s->w0 : (i == s->i1 - 1 ? s->w1 : 1.0f);
}

static uint8_t to8(float v) {
	int i = (int)(v + 0.5f);
	return (uint8_t)(i < 0 ? 0 : (i > 255 ? 255 : i));
}

int AreaScale_argb(const uint32_t* src, int sw, int sh, int spitch, uint32_t* dst, int dw, int dh, int dpitch) {
	if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
		return -1;
	// pass 1: across, into premultiplied float rows (dw × sh × 4)
	float* mid = malloc(sizeof(float) * 4 * (size_t)dw * (size_t)sh);
	if (!mid)
		return -1;
	float rx = (float)sw / dw, ry = (float)sh / dh;
	for (int x = 0; x < dw; x++) {
		Span s = spanOf(x, rx, sw);
		for (int y = 0; y < sh; y++) {
			const uint32_t* row = src + (size_t)y * spitch;
			float a = 0, r = 0, g = 0, b = 0;
			for (int i = s.i0; i < s.i1; i++) {
				uint32_t p = row[i];
				float pa = (float)(p >> 24) * weightAt(&s, i);
				a += pa;
				r += pa * ((p >> 16) & 0xFF);
				g += pa * ((p >> 8) & 0xFF);
				b += pa * (p & 0xFF);
			}
			float* m = mid + 4 * ((size_t)y * dw + x);
			m[0] = a / rx, m[1] = r / rx, m[2] = g / rx, m[3] = b / rx;
		}
	}
	// pass 2: down, then back to straight alpha
	for (int y = 0; y < dh; y++) {
		Span s = spanOf(y, ry, sh);
		for (int x = 0; x < dw; x++) {
			float a = 0, r = 0, g = 0, b = 0;
			for (int i = s.i0; i < s.i1; i++) {
				const float* m = mid + 4 * ((size_t)i * dw + x);
				float w = weightAt(&s, i);
				a += m[0] * w, r += m[1] * w, g += m[2] * w, b += m[3] * w;
			}
			a /= ry, r /= ry, g /= ry, b /= ry;
			uint32_t out = 0;
			if (a > 0.0f) // r, g, b are premultiplied × 255: divide by the alpha sum to get the straight colour
				out = (uint32_t)to8(a) << 24 | (uint32_t)to8(r / a) << 16 | (uint32_t)to8(g / a) << 8 | to8(b / a);
			dst[(size_t)y * dpitch + x] = out;
		}
	}
	free(mid);
	return 0;
}
