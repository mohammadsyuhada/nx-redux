// area_scale.c — alpha-aware area-average resampling of ARGB8888 pixels. SDL-free.
#include "area_scale.h"
#include <stdlib.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

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

// Bilinear: per output column a source index and weight (tables built once), per output row one vertical blend of the
// two source rows over the columns the window touches, then one horizontal blend a pixel. Every blend works on whole
// pixels, two channels per 32-bit word (0x00RR00BB and 0x00AA00GG lanes): c0*(256-t) + c1*t + 128 <= 65408 fits a
// lane, so each blend is the per-channel (c0*(256-t) + c1*t + 128) >> 8 exactly.
static inline uint32_t lerpWord(uint32_t a, uint32_t b, uint32_t t) { // t in 0..256
	uint32_t u = 256 - t;
	uint32_t rb = ((a & 0x00FF00FFu) * u + (b & 0x00FF00FFu) * t + 0x00800080u) >> 8;
	uint32_t ag = (((a >> 8) & 0x00FF00FFu) * u + ((b >> 8) & 0x00FF00FFu) * t + 0x00800080u) >> 8;
	return (rb & 0x00FF00FFu) | ((ag & 0x00FF00FFu) << 8);
}

// out[i] = lerp(r0[i], r1[i], t) for i in [0, n)
static void lerpRows(const uint32_t* r0, const uint32_t* r1, uint32_t* out, int n, uint32_t t) {
	int i = 0;
	if (t == 0 || r0 == r1) {
		for (; i < n; i++)
			out[i] = r0[i];
		return;
	}
#ifdef __ARM_NEON
	// 4 pixels a step, 16-bit lanes: same exact (c0*(256-t) + c1*t + 128) >> 8
	uint16x8_t vu = vdupq_n_u16((uint16_t)(256 - t)), vt = vdupq_n_u16((uint16_t)t);
	for (; i + 4 <= n; i += 4) {
		uint8x16_t a = vld1q_u8((const uint8_t*)(r0 + i)), b = vld1q_u8((const uint8_t*)(r1 + i));
		uint16x8_t lo = vmlaq_u16(vmulq_u16(vmovl_u8(vget_low_u8(a)), vu), vmovl_u8(vget_low_u8(b)), vt);
		uint16x8_t hi = vmlaq_u16(vmulq_u16(vmovl_u8(vget_high_u8(a)), vu), vmovl_u8(vget_high_u8(b)), vt);
		vst1q_u8((uint8_t*)(out + i), vcombine_u8(vrshrn_n_u16(lo, 8), vrshrn_n_u16(hi, 8)));
	}
#endif
	for (; i < n; i++)
		out[i] = lerpWord(r0[i], r1[i], t);
}

int AreaScale_bilinearCover(const uint32_t* src, int sw, int sh, int spitch, uint32_t* dst, int dw, int dh, int dpitch,
							float s, float ox, float oy) {
	if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || !(s > 0.0f))
		return -1;
	int* x0s = malloc(sizeof(int) * (size_t)dw);
	uint16_t* txs = malloc(sizeof(uint16_t) * (size_t)dw);
	uint32_t* mid = malloc(sizeof(uint32_t) * ((size_t)sw + 1));
	if (!x0s || !txs || !mid) {
		free(x0s);
		free(txs);
		free(mid);
		return -1;
	}
	int maxx = sw - 1, maxy = sh - 1;
	for (int x = 0; x < dw; x++) {
		float fx = (x + 0.5f + ox) / s - 0.5f;
		if (fx < 0)
			fx = 0;
		if (fx > maxx)
			fx = maxx;
		int x0 = (int)fx;
		x0s[x] = x0;
		txs[x] = x0 < maxx ? (uint16_t)((fx - x0) * 256) : 0; // the last column blends with itself
	}
	// only the columns the window reads: [lo, hi]
	int lo = x0s[0], hi = x0s[dw - 1] < maxx ? x0s[dw - 1] + 1 : maxx;
	mid[sw] = 0;
	for (int y = 0; y < dh; y++) {
		float fy = (y + 0.5f + oy) / s - 0.5f;
		if (fy < 0)
			fy = 0;
		if (fy > maxy)
			fy = maxy;
		int y0 = (int)fy, y1 = y0 < maxy ? y0 + 1 : y0;
		uint32_t ty = (uint32_t)((fy - y0) * 256);
		lerpRows(src + (size_t)y0 * spitch + lo, src + (size_t)y1 * spitch + lo, mid + lo, hi - lo + 1, ty);
		uint32_t* row = dst + (size_t)y * dpitch;
		for (int x = 0; x < dw; x++) {
			const uint32_t* p = mid + x0s[x];
			row[x] = txs[x] ? lerpWord(p[0], p[1], txs[x]) : p[0];
		}
	}
	free(x0s);
	free(txs);
	free(mid);
	return 0;
}
