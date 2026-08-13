// Host unit test for text_shape.c (pure, no SDL).
// Build & run (from workspace/all/common/):
//   cc -I. text_shape.c tests/test_text_shape.c -o /tmp/test_text_shape && /tmp/test_text_shape
//
// Output is VISUAL order: contextual shaping (forms + lam-alef) followed by
// pragmatic BiDi reordering (RTL runs reversed; embedded Latin/numbers kept
// LTR; combining marks stay attached to their base).
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../text_shape.h"

static void test_has_arabic(void) {
	assert(TextShape_hasArabic("hello world") == false);
	assert(TextShape_hasArabic("") == false);
	assert(TextShape_hasArabic("\xE4\xB8\xAD\xE6\x96\x87") == false);		 // 中文 (CJK)
	assert(TextShape_hasArabic("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85") == true); // سلام
	assert(TextShape_hasArabic("FM \xD9\x86\xD9\x88\xD8\xB1") == true);		 // "FM نور"
}

static void expect(const char* in, const char* exp) {
	char out[512];
	int n = TextShape_toVisual(in, out, sizeof(out));
	assert(n == (int)strlen(exp));
	assert(strcmp(out, exp) == 0);
}

// سلام (seen-lam-alef-meem): shaped [FEB3,FEFC,FEE1], RTL -> visual reversed.
static void test_shape_salam(void) {
	expect("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85",
		   "\xEF\xBB\xA1\xEF\xBB\xBC\xEF\xBA\xB3"); // FEE1 FEFC FEB3
}

// لا (lam+alef) -> single isolated lam-alef ligature FEFB.
static void test_shape_lam_alef(void) {
	expect("\xD9\x84\xD8\xA7", "\xEF\xBB\xBB"); // FEFB
}

// نور (noon-waw-reh): shaped [FEE7,FEEE,FEAD], RTL -> visual reversed.
static void test_shape_noor(void) {
	expect("\xD9\x86\xD9\x88\xD8\xB1",
		   "\xEF\xBA\xAD\xEF\xBB\xAE\xEF\xBB\xA7"); // FEAD FEEE FEE7
}

// بَت (beh + fatha + teh): fatha is transparent; shaped [FE91,064E,FE96],
// visual reverses the two clusters but keeps the fatha after its base beh.
static void test_shape_harakat_transparent(void) {
	expect("\xD8\xA8\xD9\x8E\xD8\xAA",
		   "\xEF\xBA\x96\xEF\xBA\x91\xD9\x8E"); // FE96 FE91 064E
}

// Non-Arabic passes through byte-for-byte (base LTR, no reordering).
static void test_passthrough(void) {
	expect("Radio 90.5 FM", "Radio 90.5 FM");
	expect("", "");
}

// LTR-base with an embedded Arabic run: "FM نور" -> "FM " stays put, the
// Arabic run is reversed to visual order.
static void test_mixed_ltr_base(void) {
	expect("FM \xD9\x86\xD9\x88\xD8\xB1",
		   "FM \xEF\xBA\xAD\xEF\xBB\xAE\xEF\xBB\xA7"); // "FM " + FEAD FEEE FEE7
}

static void test_base_dir(void) {
	assert(TextShape_baseIsRTL("hello") == false);
	assert(TextShape_baseIsRTL("123") == false);						 // digits -> LTR base
	assert(TextShape_baseIsRTL("\xD9\x86\xD9\x88\xD8\xB1") == true);	 // نور
	assert(TextShape_baseIsRTL("FM \xD9\x86\xD9\x88\xD8\xB1") == false); // Latin first -> LTR base
	assert(TextShape_baseIsRTL("\xD9\x86 FM") == true);					 // Arabic first -> RTL base
}

int main(void) {
	test_has_arabic();
	test_base_dir();
	test_shape_salam();
	test_shape_lam_alef();
	test_shape_noor();
	test_shape_harakat_transparent();
	test_passthrough();
	test_mixed_ltr_base();
	printf("all text_shape tests passed\n");
	return 0;
}
