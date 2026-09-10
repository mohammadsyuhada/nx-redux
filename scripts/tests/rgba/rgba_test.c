// Host unit test for workspace/all/common/rgba.h (theme colour packing).
#include <stdio.h>
#include <string.h>
#include "rgba.h"

static int failures = 0;
#define CHECK_EQ_U32(expr, want)                                                        \
	do {                                                                                \
		uint32_t got_ = (expr);                                                         \
		if (got_ != (uint32_t)(want)) {                                                 \
			printf("FAIL %s:%d: %s = 0x%08X, want 0x%08X\n", __FILE__, __LINE__, #expr, \
				   got_, (uint32_t)(want));                                             \
			failures++;                                                                 \
		}                                                                               \
	} while (0)
#define CHECK_EQ_INT(expr, want)                                                      \
	do {                                                                              \
		int got_ = (expr);                                                            \
		if (got_ != (want)) {                                                         \
			printf("FAIL %s:%d: %s = %d, want %d\n", __FILE__, __LINE__, #expr, got_, \
				   (want));                                                           \
			failures++;                                                               \
		}                                                                             \
	} while (0)

int main(void) {
	// pack / unpack
	CHECK_EQ_U32(RGBA_pack(0x12, 0x34, 0x56, 0x78), 0x12345678);
	CHECK_EQ_INT(RGBA_r(0x12345678), 0x12);
	CHECK_EQ_INT(RGBA_g(0x12345678), 0x34);
	CHECK_EQ_INT(RGBA_b(0x12345678), 0x56);
	CHECK_EQ_INT(RGBA_a(0x12345678), 0x78);
	CHECK_EQ_U32(RGBA_pack(300, -1, 0, 256), 0xFF0000FF); // clamped

	// alpha helpers
	CHECK_EQ_U32(RGBA_withAlpha(0x123456FF, 0x80), 0x12345680);
	CHECK_EQ_U32(RGBA_withAlpha(0x12345600, 999), 0x123456FF);
	CHECK_EQ_U32(RGBA_fromRGB(0x9B2257), 0x9B2257FF);
	CHECK_EQ_U32(RGBA_fromRGB(0xFF9B2257), 0x9B2257FF); // high byte ignored
	CHECK_EQ_U32(RGBA_toRGB(0x9B225780), 0x9B2257);

	// parse: legacy 6-digit RGB (with/without 0x, whitespace) -> opaque
	CHECK_EQ_U32(RGBA_parseHex("0x9B2257"), 0x9B2257FF);
	CHECK_EQ_U32(RGBA_parseHex("9b2257"), 0x9B2257FF);
	CHECK_EQ_U32(RGBA_parseHex("  0X9B2257\n"), 0x9B2257FF);
	CHECK_EQ_U32(RGBA_parseHex("0x000000"), 0x000000FF);
	CHECK_EQ_U32(RGBA_parseHex("0xFFFFFF"), 0xFFFFFFFF);
	// short legacy values are still RGB
	CHECK_EQ_U32(RGBA_parseHex("0x22"), 0x000022FF);
	CHECK_EQ_U32(RGBA_parseHex("0"), 0x000000FF);
	// 8-digit RGBA as written
	CHECK_EQ_U32(RGBA_parseHex("0x9B225780"), 0x9B225780);
	CHECK_EQ_U32(RGBA_parseHex("9B225780"), 0x9B225780);
	CHECK_EQ_U32(RGBA_parseHex("0x00000000"), 0x00000000);
	// 7 digits = 8-digit value with the leading zero dropped
	CHECK_EQ_U32(RGBA_parseHex("0x9B22578"), 0x09B22578);
	// more than 8 digits: first 8 win
	CHECK_EQ_U32(RGBA_parseHex("0x9B225780AB"), 0x9B225780);
	// garbage / empty
	CHECK_EQ_U32(RGBA_parseHex(""), 0x000000FF);
	CHECK_EQ_U32(RGBA_parseHex("zz"), 0x000000FF);
	CHECK_EQ_U32(RGBA_parseHex("0x"), 0x000000FF);
	// trailing junk after digits is ignored
	CHECK_EQ_U32(RGBA_parseHex("0x9B2257 # comment"), 0x9B2257FF);

	// percent conversions
	CHECK_EQ_INT(RGBA_percentToAlpha(100), 255);
	CHECK_EQ_INT(RGBA_percentToAlpha(0), 0);
	CHECK_EQ_INT(RGBA_percentToAlpha(50), 128);
	CHECK_EQ_INT(RGBA_percentToAlpha(150), 255);
	CHECK_EQ_INT(RGBA_percentToAlpha(-5), 0);
	CHECK_EQ_INT(RGBA_alphaToPercent(255), 100);
	CHECK_EQ_INT(RGBA_alphaToPercent(0), 0);
	CHECK_EQ_INT(RGBA_alphaToPercent(128), 50);
	for (int pct = 10; pct <= 100; pct += 10)
		CHECK_EQ_INT(RGBA_alphaToPercent(RGBA_percentToAlpha(pct)), pct);

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("rgba_test: all checks passed\n");
	return 0;
}
