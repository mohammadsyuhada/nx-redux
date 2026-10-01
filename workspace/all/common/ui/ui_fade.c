// ui_fade.c — cached black gradient surfaces built from the ui_ease.c curves
#include "ui_fade.h"
#include "ui_ease.h"
#include <string.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#define FADE_SLOTS 6

typedef enum { FADE_EASED = 1,
			   FADE_BAND } FadeKind;

typedef struct {
	FadeKind kind; // 0 = empty slot
	int w, h;
	float edge;
	float param; // eased: power, band: hold
	bool dark_at_top;
	SDL_Surface* surface;
	unsigned last_use;
} FadeSlot;

static FadeSlot slots[FADE_SLOTS];
static unsigned use_clock = 0;

static SDL_Surface* findSlot(FadeKind kind, int w, int h, float edge, float param, bool dark_at_top) {
	for (int i = 0; i < FADE_SLOTS; i++) {
		FadeSlot* s = &slots[i];
		if (s->kind == kind && s->surface && s->w == w && s->h == h && s->edge == edge &&
			s->param == param && s->dark_at_top == dark_at_top) {
			s->last_use = ++use_clock;
			return s->surface;
		}
	}
	return NULL;
}

// Store a new surface, evicting (and freeing) the least recently used slot.
static void storeSlot(FadeKind kind, int w, int h, float edge, float param, bool dark_at_top, SDL_Surface* surface) {
	FadeSlot* victim = &slots[0];
	for (int i = 0; i < FADE_SLOTS; i++) {
		if (!slots[i].surface) {
			victim = &slots[i];
			break;
		}
		if (slots[i].last_use < victim->last_use)
			victim = &slots[i];
	}
	if (victim->surface)
		SDL_FreeSurface(victim->surface);
	victim->kind = kind;
	victim->w = w;
	victim->h = h;
	victim->edge = edge;
	victim->param = param;
	victim->dark_at_top = dark_at_top;
	victim->surface = surface;
	victim->last_use = ++use_clock;
}

static SDL_Surface* createFade(int w, int h) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (s)
		SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	return s;
}

static void fillRow(SDL_Surface* s, int row, float alpha) {
	SDL_Rect r = {0, row, s->w, 1};
	SDL_FillRect(s, &r, SDL_MapRGBA(s->format, 0, 0, 0, (Uint8)(alpha * 255 + 0.5f)));
}

SDL_Surface* UI_easedFadeSurface(int w, int h, float edge, float power, bool dark_at_top) {
	if (w <= 0 || h <= 0)
		return NULL;
	SDL_Surface* s = findSlot(FADE_EASED, w, h, edge, power, dark_at_top);
	if (s)
		return s;
	s = createFade(w, h);
	if (!s)
		return NULL;
	float span = (float)(h > 1 ? h - 1 : 1);
	for (int row = 0; row < h; row++) {
		int dist = dark_at_top ? row : h - 1 - row;
		fillRow(s, row, UI_easedFadeAlpha(edge, (float)dist / span, power));
	}
	storeSlot(FADE_EASED, w, h, edge, power, dark_at_top, s);
	return s;
}

SDL_Surface* UI_bandFadeSurface(int w, int h, float edge, int hold) {
	if (w <= 0 || h <= 0)
		return NULL;
	SDL_Surface* s = findSlot(FADE_BAND, w, h, edge, (float)hold, false);
	if (s)
		return s;
	s = createFade(w, h);
	if (!s)
		return NULL;
	for (int row = 0; row < h; row++)
		fillRow(s, row, UI_linearHoldFadeAlpha(edge, (float)(h - 1 - row), (float)(h - 1), (float)hold));
	storeSlot(FADE_BAND, w, h, edge, (float)hold, false, s);
	return s;
}

#ifdef __ARM_NEON
// Two 8-lane 16-bit products → 16 bytes, each x / 255 exact: (x + 128 + ((x + 128) >> 8)) >> 8.
static inline uint8x16_t div255(uint16x8_t lo, uint16x8_t hi) {
	return vcombine_u8(vraddhn_u16(lo, vrshrq_n_u16(lo, 8)), vraddhn_u16(hi, vrshrq_n_u16(hi, 8)));
}
#endif

static bool isArgb(const SDL_Surface* s) {
	return s && s->format->format == SDL_PIXELFORMAT_ARGB8888 && !SDL_MUSTLOCK(s);
}

// Black at alpha a over w pixels: every channel × (255 − a) / 255 (exact), then + a on the alpha byte.
static void darkenRow(Uint32* p, int w, Uint32 a) {
	Uint32 k = 255 - a;
	int i = 0;
#ifdef __ARM_NEON
	// 4 pixels a step: every channel × k / 255 (x + 128 + ((x + 128) >> 8)) >> 8, then + a on the alpha bytes
	uint8x8_t vk = vdup_n_u8((uint8_t)k);
	uint8x16_t va = vreinterpretq_u8_u32(vdupq_n_u32(a << 24));
	// 16 pixels a step (four independent chains keep the in-order A53 busy), then 4
	for (; i + 16 <= w; i += 16) {
		uint8x16_t d0 = vld1q_u8((const uint8_t*)(p + i)), d1 = vld1q_u8((const uint8_t*)(p + i + 4));
		uint8x16_t d2 = vld1q_u8((const uint8_t*)(p + i + 8)), d3 = vld1q_u8((const uint8_t*)(p + i + 12));
		uint16x8_t a0 = vmull_u8(vget_low_u8(d0), vk), b0 = vmull_u8(vget_high_u8(d0), vk);
		uint16x8_t a1 = vmull_u8(vget_low_u8(d1), vk), b1 = vmull_u8(vget_high_u8(d1), vk);
		uint16x8_t a2 = vmull_u8(vget_low_u8(d2), vk), b2 = vmull_u8(vget_high_u8(d2), vk);
		uint16x8_t a3 = vmull_u8(vget_low_u8(d3), vk), b3 = vmull_u8(vget_high_u8(d3), vk);
		vst1q_u8((uint8_t*)(p + i), vaddq_u8(div255(a0, b0), va));
		vst1q_u8((uint8_t*)(p + i + 4), vaddq_u8(div255(a1, b1), va));
		vst1q_u8((uint8_t*)(p + i + 8), vaddq_u8(div255(a2, b2), va));
		vst1q_u8((uint8_t*)(p + i + 12), vaddq_u8(div255(a3, b3), va));
	}
	for (; i + 4 <= w; i += 4) {
		uint8x16_t dv = vld1q_u8((const uint8_t*)(p + i));
		vst1q_u8((uint8_t*)(p + i), vaddq_u8(div255(vmull_u8(vget_low_u8(dv), vk), vmull_u8(vget_high_u8(dv), vk)), va));
	}
#endif
	for (; i < w; i++) {
		// rgb × (255 − a) / 255, alpha a + A × (255 − a) / 255, two 8-bit lanes per multiply (exact /255)
		Uint32 d = p[i];
		Uint32 rb = (d & 0x00FF00FFu) * k + 0x00800080u;
		rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
		Uint32 ag = ((d >> 8) & 0x00FF00FFu) * k + 0x00800080u;
		ag = ((ag + ((ag >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
		p[i] = rb | ((ag + (a << 16)) << 8);
	}
}

void UI_blitFade(SDL_Surface* fade, const SDL_Rect* src, SDL_Surface* dst, int x, int y) {
	if (!fade || !dst)
		return;
	SDL_Rect sr = src ? *src : (SDL_Rect){0, 0, fade->w, fade->h};
	if (fade->format->format != SDL_PIXELFORMAT_ARGB8888 || !isArgb(dst) || sr.x < 0 || sr.y < 0 ||
		sr.x + sr.w > fade->w || sr.y + sr.h > fade->h) {
		SDL_BlitSurface(fade, src, dst, &(SDL_Rect){x, y});
		return;
	}
	SDL_Rect out = {x, y, sr.w, sr.h}, clipped;
	if (!SDL_IntersectRect(&out, &dst->clip_rect, &clipped))
		return;
	for (int dy = clipped.y; dy < clipped.y + clipped.h; dy++) {
		const Uint32* frow = (const Uint32*)((const Uint8*)fade->pixels + (sr.y + dy - y) * fade->pitch);
		Uint32 a = frow[sr.x] >> 24; // black, one alpha per row
		if (a == 0)
			continue;
		darkenRow((Uint32*)((Uint8*)dst->pixels + dy * dst->pitch) + clipped.x, clipped.w, a);
	}
}

#define FADE_H_MAX 4096

void UI_darkenColumns(SDL_Surface* dst, int x, int y, int w, int h, const Uint8* alpha) {
	if (!dst || !alpha || w <= 0 || h <= 0 || w > FADE_H_MAX)
		return;
	SDL_Rect out = {x, y, w, h}, c;
	if (!SDL_IntersectRect(&out, &dst->clip_rect, &c))
		return;
	const Uint8* al = alpha + (c.x - x);
	// trim clear columns at both ends
	int lo = 0, hi = c.w;
	while (lo < hi && al[lo] == 0)
		lo++;
	while (hi > lo && al[hi - 1] == 0)
		hi--;
	if (lo >= hi)
		return;
	if (!isArgb(dst)) {
		for (int i = lo; i < hi; i++)
			UI_dimRect(dst, &(SDL_Rect){c.x + i, c.y, 1, c.h}, al[i]);
		return;
	}
	// per column: k = 255 − a in every byte (the multiplier) and a on the alpha byte (the add)
	static Uint32 kk[FADE_H_MAX], aa[FADE_H_MAX];
	int n = hi - lo;
	for (int i = 0; i < n; i++) {
		Uint32 a = al[lo + i];
		kk[i] = (255 - a) * 0x01010101u;
		aa[i] = a << 24;
	}
	for (int dy = c.y; dy < c.y + c.h; dy++) {
		Uint32* p = (Uint32*)((Uint8*)dst->pixels + dy * dst->pitch) + c.x + lo;
		int i = 0;
#ifdef __ARM_NEON
		for (; i + 16 <= n; i += 16) { // 16 pixels a step: four independent chains for the in-order A53
			uint8x16_t d[4], k[4];
			uint16x8_t lo[4], hi[4];
			for (int j = 0; j < 4; j++) {
				d[j] = vld1q_u8((const uint8_t*)(p + i + 4 * j));
				k[j] = vld1q_u8((const uint8_t*)(kk + i + 4 * j));
			}
			for (int j = 0; j < 4; j++) {
				lo[j] = vmull_u8(vget_low_u8(d[j]), vget_low_u8(k[j]));
				hi[j] = vmull_u8(vget_high_u8(d[j]), vget_high_u8(k[j]));
			}
			for (int j = 0; j < 4; j++)
				vst1q_u8((uint8_t*)(p + i + 4 * j), vaddq_u8(div255(lo[j], hi[j]), vld1q_u8((const uint8_t*)(aa + i + 4 * j))));
		}
		for (; i + 4 <= n; i += 4) {
			uint8x16_t dv = vld1q_u8((const uint8_t*)(p + i)), kv = vld1q_u8((const uint8_t*)(kk + i));
			uint8x16_t o = div255(vmull_u8(vget_low_u8(dv), vget_low_u8(kv)), vmull_u8(vget_high_u8(dv), vget_high_u8(kv)));
			vst1q_u8((uint8_t*)(p + i), vaddq_u8(o, vld1q_u8((const uint8_t*)(aa + i))));
		}
#endif
		for (; i < n; i++) {
			Uint32 k = kk[i] & 0xFF, d = p[i];
			Uint32 rb = (d & 0x00FF00FFu) * k + 0x00800080u;
			rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
			Uint32 ag = ((d >> 8) & 0x00FF00FFu) * k + 0x00800080u;
			ag = ((ag + ((ag >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
			p[i] = (rb | (ag << 8)) + aa[i];
		}
	}
}

void UI_dimRect(SDL_Surface* dst, const SDL_Rect* r, Uint8 alpha) {
	if (!dst || alpha == 0)
		return;
	SDL_Rect want = r ? *r : (SDL_Rect){0, 0, dst->w, dst->h}, c;
	if (!SDL_IntersectRect(&want, &dst->clip_rect, &c))
		return;
	if (alpha == 255) {
		SDL_FillRect(dst, &c, SDL_MapRGBA(dst->format, 0, 0, 0, 255));
		return;
	}
	if (!isArgb(dst)) {
		SDL_Surface* black = SDL_CreateRGBSurfaceWithFormat(0, c.w, c.h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!black)
			return;
		SDL_FillRect(black, NULL, SDL_MapRGBA(black->format, 0, 0, 0, alpha));
		SDL_SetSurfaceBlendMode(black, SDL_BLENDMODE_BLEND);
		SDL_BlitSurface(black, NULL, dst, &c);
		SDL_FreeSurface(black);
		return;
	}
	for (int dy = c.y; dy < c.y + c.h; dy++)
		darkenRow((Uint32*)((Uint8*)dst->pixels + dy * dst->pitch) + c.x, c.w, alpha);
}

void UI_blitOpaque(SDL_Surface* src, SDL_Surface* under, int sx, int sy, int w, int h, SDL_Surface* dst, int dx,
				   int dy, int alpha) {
	if (!src || !dst || alpha <= 0)
		return;
	if (!isArgb(src) || !isArgb(dst) || (under && !isArgb(under))) {
		if (under) {
			SDL_BlendMode bm;
			SDL_GetSurfaceBlendMode(under, &bm);
			SDL_SetSurfaceBlendMode(under, SDL_BLENDMODE_NONE);
			SDL_BlitSurface(under, &(SDL_Rect){sx, sy, w, h}, dst, &(SDL_Rect){dx, dy});
			SDL_SetSurfaceBlendMode(under, bm);
		}
		Uint8 oa;
		SDL_BlendMode bm;
		SDL_GetSurfaceAlphaMod(src, &oa);
		SDL_GetSurfaceBlendMode(src, &bm);
		SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
		SDL_SetSurfaceAlphaMod(src, (Uint8)(alpha > 255 ? 255 : alpha));
		SDL_BlitSurface(src, &(SDL_Rect){sx, sy, w, h}, dst, &(SDL_Rect){dx, dy});
		SDL_SetSurfaceAlphaMod(src, oa);
		SDL_SetSurfaceBlendMode(src, bm);
		return;
	}
	SDL_Rect out = {dx, dy, w, h}, c;
	if (!SDL_IntersectRect(&out, &dst->clip_rect, &c))
		return;
	sx += c.x - dx;
	sy += c.y - dy;
	Uint32 a = (Uint32)(alpha > 255 ? 255 : alpha), ia = 255 - a;
	for (int y = 0; y < c.h; y++) {
		const Uint32* s = (const Uint32*)((const Uint8*)src->pixels + (sy + y) * src->pitch) + sx;
		Uint32* d = (Uint32*)((Uint8*)dst->pixels + (c.y + y) * dst->pitch) + c.x;
		const Uint32* u = under ? (const Uint32*)((const Uint8*)under->pixels + (sy + y) * under->pitch) + sx : d;
		if (a == 255) {
			memcpy(d, s, (size_t)c.w * 4);
			continue;
		}
		int i = 0;
#ifdef __ARM_NEON
		// 4 pixels (16 channels) a step: x = s·a + u·(255 − a) in 16 bits, then (x + 128 + ((x + 128) >> 8)) >> 8
		uint8x8_t va = vdup_n_u8((uint8_t)a), via = vdup_n_u8((uint8_t)ia);
		for (; i + 16 <= c.w; i += 16) { // 16 pixels a step: four independent chains for the in-order A53
			uint8x16_t sv[4], uv[4];
			uint16x8_t lo[4], hi[4];
			for (int j = 0; j < 4; j++) {
				sv[j] = vld1q_u8((const uint8_t*)(s + i + 4 * j));
				uv[j] = vld1q_u8((const uint8_t*)(u + i + 4 * j));
			}
			for (int j = 0; j < 4; j++) {
				lo[j] = vmlal_u8(vmull_u8(vget_low_u8(sv[j]), va), vget_low_u8(uv[j]), via);
				hi[j] = vmlal_u8(vmull_u8(vget_high_u8(sv[j]), va), vget_high_u8(uv[j]), via);
			}
			for (int j = 0; j < 4; j++)
				vst1q_u8((uint8_t*)(d + i + 4 * j), div255(lo[j], hi[j]));
		}
		for (; i + 4 <= c.w; i += 4) {
			uint8x16_t sv = vld1q_u8((const uint8_t*)(s + i)), uv = vld1q_u8((const uint8_t*)(u + i));
			uint16x8_t lo = vmlal_u8(vmull_u8(vget_low_u8(sv), va), vget_low_u8(uv), via);
			uint16x8_t hi = vmlal_u8(vmull_u8(vget_high_u8(sv), va), vget_high_u8(uv), via);
			vst1q_u8((uint8_t*)(d + i), div255(lo, hi));
		}
#endif
		for (; i < c.w; i++) {
			// (s·a + u·(255 − a)) / 255 per channel, two 8-bit lanes per multiply (exact /255)
			Uint32 rb = (s[i] & 0x00FF00FFu) * a + (u[i] & 0x00FF00FFu) * ia + 0x00800080u;
			rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
			Uint32 ag = ((s[i] >> 8) & 0x00FF00FFu) * a + ((u[i] >> 8) & 0x00FF00FFu) * ia + 0x00800080u;
			ag = ((ag + ((ag >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
			d[i] = rb | (ag << 8);
		}
	}
}

void UI_blitBlendOpaque(SDL_Surface* src, const SDL_Rect* srect, SDL_Surface* dst, int dx, int dy, int alpha) {
	if (!src || !dst || alpha <= 0)
		return;
	SDL_Rect sr = srect ? *srect : (SDL_Rect){0, 0, src->w, src->h};
	if (alpha > 255)
		alpha = 255;
	if (!isArgb(src) || !isArgb(dst) || sr.x < 0 || sr.y < 0 || sr.x + sr.w > src->w || sr.y + sr.h > src->h) {
		Uint8 oa;
		SDL_BlendMode bm;
		SDL_GetSurfaceAlphaMod(src, &oa);
		SDL_GetSurfaceBlendMode(src, &bm);
		SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
		SDL_SetSurfaceAlphaMod(src, (Uint8)alpha);
		SDL_BlitSurface(src, &sr, dst, &(SDL_Rect){dx, dy});
		SDL_SetSurfaceAlphaMod(src, oa);
		SDL_SetSurfaceBlendMode(src, bm);
		return;
	}
	SDL_Rect out = {dx, dy, sr.w, sr.h}, c;
	if (!SDL_IntersectRect(&out, &dst->clip_rect, &c))
		return;
	int sx = sr.x + c.x - dx, sy = sr.y + c.y - dy;
	Uint32 g = (Uint32)alpha;
	for (int y = 0; y < c.h; y++) {
		const Uint32* s = (const Uint32*)((const Uint8*)src->pixels + (sy + y) * src->pitch) + sx;
		Uint32* d = (Uint32*)((Uint8*)dst->pixels + (c.y + y) * dst->pitch) + c.x;
		int i = 0;
#ifdef __ARM_NEON
		// 4 pixels a step: a = A·g / 255 per pixel (exact), copied into its 4 bytes, then the opaque lerp
		uint32x4_t round = vdupq_n_u32(128);
		uint8x16_t opaque = vreinterpretq_u8_u32(vdupq_n_u32(0xFF000000u));
		for (; i + 4 <= c.w; i += 4) {
			uint32x4_t sv = vld1q_u32(s + i);
			uint32x4_t x = vmlaq_n_u32(round, vshrq_n_u32(sv, 24), g);
			uint32x4_t a = vshrq_n_u32(vaddq_u32(x, vshrq_n_u32(x, 8)), 8);
			if (vmaxvq_u32(a) == 0)
				continue; // clear: dst stays
			uint8x16_t s8 = vreinterpretq_u8_u32(sv);
			if (vminvq_u32(a) == 255) {
				vst1q_u8((uint8_t*)(d + i), vorrq_u8(s8, opaque));
				continue;
			}
			uint8x16_t a8 = vreinterpretq_u8_u32(vmulq_n_u32(a, 0x01010101u)), ia8 = vmvnq_u8(a8);
			uint8x16_t d8 = vld1q_u8((const uint8_t*)(d + i));
			uint16x8_t lo = vmlal_u8(vmull_u8(vget_low_u8(s8), vget_low_u8(a8)), vget_low_u8(d8), vget_low_u8(ia8));
			uint16x8_t hi =
				vmlal_u8(vmull_u8(vget_high_u8(s8), vget_high_u8(a8)), vget_high_u8(d8), vget_high_u8(ia8));
			uint8x16_t r = vcombine_u8(vraddhn_u16(lo, vrshrq_n_u16(lo, 8)), vraddhn_u16(hi, vrshrq_n_u16(hi, 8)));
			vst1q_u8((uint8_t*)(d + i), vorrq_u8(r, opaque));
		}
#endif
		for (; i < c.w; i++) {
			Uint32 x = (s[i] >> 24) * g + 128;
			Uint32 a = (x + (x >> 8)) >> 8;
			if (a == 0)
				continue;
			if (a == 255) {
				d[i] = s[i] | 0xFF000000u;
				continue;
			}
			Uint32 ia = 255 - a;
			Uint32 rb = (s[i] & 0x00FF00FFu) * a + (d[i] & 0x00FF00FFu) * ia + 0x00800080u;
			rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
			Uint32 gg = ((s[i] >> 8) & 0xFFu) * a + ((d[i] >> 8) & 0xFFu) * ia + 0x80u;
			gg = ((gg + (gg >> 8)) >> 8) & 0xFFu;
			d[i] = 0xFF000000u | rb | (gg << 8);
		}
	}
}

void UI_fadeCacheClear(void) {
	for (int i = 0; i < FADE_SLOTS; i++) {
		if (slots[i].surface)
			SDL_FreeSurface(slots[i].surface);
		slots[i] = (FadeSlot){0};
	}
	use_clock = 0;
}
