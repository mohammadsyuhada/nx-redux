// Packed theme colour helpers. Theme colours are 0xRRGGBBAA (alpha in the low
// byte, 0xFF = opaque). This header has no SDL dependency so it can be unit
// tested on the host; api.c wraps it for SDL_Color / screen-mapped values.
#ifndef RGBA_H
#define RGBA_H

#include <stdint.h>

static inline int RGBA_clamp255(int v) {
	return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static inline uint32_t RGBA_pack(int r, int g, int b, int a) {
	return ((uint32_t)RGBA_clamp255(r) << 24) | ((uint32_t)RGBA_clamp255(g) << 16) |
		   ((uint32_t)RGBA_clamp255(b) << 8) | (uint32_t)RGBA_clamp255(a);
}

static inline int RGBA_r(uint32_t c) {
	return (int)((c >> 24) & 0xFF);
}
static inline int RGBA_g(uint32_t c) {
	return (int)((c >> 16) & 0xFF);
}
static inline int RGBA_b(uint32_t c) {
	return (int)((c >> 8) & 0xFF);
}
static inline int RGBA_a(uint32_t c) {
	return (int)(c & 0xFF);
}

static inline uint32_t RGBA_withAlpha(uint32_t c, int a) {
	return (c & 0xFFFFFF00u) | (uint32_t)RGBA_clamp255(a);
}

// 0xRRGGBB (any high byte ignored) -> opaque 0xRRGGBBFF
static inline uint32_t RGBA_fromRGB(uint32_t rgb) {
	return ((rgb & 0x00FFFFFFu) << 8) | 0xFFu;
}

// 0xRRGGBBAA -> 0xRRGGBB
static inline uint32_t RGBA_toRGB(uint32_t rgba) {
	return rgba >> 8;
}

static inline int RGBA_hexDigit(char ch) {
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}

// Parse a settings-file colour. Decides by digit count, never by value:
//   1-6 hex digits  -> legacy 0xRRGGBB, returned opaque
//   7-8 hex digits  -> packed 0xRRGGBBAA as written
//   >8 digits       -> first 8 digits
//   no digits       -> opaque black
// Accepts leading whitespace and an optional 0x/0X prefix; stops at the first
// non-hex character.
static inline uint32_t RGBA_parseHex(const char* s) {
	if (!s)
		return 0x000000FFu;
	while (*s == ' ' || *s == '\t')
		s++;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	uint32_t value = 0;
	int digits = 0;
	for (; digits < 8; digits++) {
		int d = RGBA_hexDigit(s[digits]);
		if (d < 0)
			break;
		value = (value << 4) | (uint32_t)d;
	}
	if (digits == 0)
		return 0x000000FFu;
	if (digits <= 6)
		return RGBA_fromRGB(value);
	return value;
}

// 0..255 -> 0..100, rounded to nearest
static inline int RGBA_alphaToPercent(int a) {
	a = RGBA_clamp255(a);
	return (a * 100 + 127) / 255;
}

// 0..100 -> 0..255, rounded to nearest, clamped
static inline int RGBA_percentToAlpha(int pct) {
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	return (pct * 255 + 50) / 100;
}

#endif // RGBA_H
