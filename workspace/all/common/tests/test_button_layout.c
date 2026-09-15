// Host unit test for button_layout.h (pure, no SDL). Run via run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../button_layout.h"

static void test_swap_face(void) {
	// Nintendo: identity
	assert(ButtonLayout_swapFace(BL_FACE_A, 0) == BL_FACE_A);
	assert(ButtonLayout_swapFace(BL_FACE_B, 0) == BL_FACE_B);
	assert(ButtonLayout_swapFace(BL_FACE_X, 0) == BL_FACE_X);
	assert(ButtonLayout_swapFace(BL_FACE_Y, 0) == BL_FACE_Y);
	// Xbox: A<->B, X<->Y
	assert(ButtonLayout_swapFace(BL_FACE_A, 1) == BL_FACE_B);
	assert(ButtonLayout_swapFace(BL_FACE_B, 1) == BL_FACE_A);
	assert(ButtonLayout_swapFace(BL_FACE_X, 1) == BL_FACE_Y);
	assert(ButtonLayout_swapFace(BL_FACE_Y, 1) == BL_FACE_X);
	// NONE passes through either way
	assert(ButtonLayout_swapFace(BL_FACE_NONE, 1) == BL_FACE_NONE);
	// swapping twice is the identity
	assert(ButtonLayout_swapFace(ButtonLayout_swapFace(BL_FACE_X, 1), 1) == BL_FACE_X);
}

static void test_display_label(void) {
	// logical mode: pointer returned unchanged
	const char* a = "A";
	assert(ButtonLayout_displayLabel(a, 0) == a);
	assert(strcmp(ButtonLayout_displayLabel("MENU+A", 0), "MENU+A") == 0);
	// physical mode: the four face letters swap
	assert(strcmp(ButtonLayout_displayLabel("A", 1), "B") == 0);
	assert(strcmp(ButtonLayout_displayLabel("B", 1), "A") == 0);
	assert(strcmp(ButtonLayout_displayLabel("X", 1), "Y") == 0);
	assert(strcmp(ButtonLayout_displayLabel("Y", 1), "X") == 0);
	assert(strcmp(ButtonLayout_displayLabel("MENU+A", 1), "MENU+B") == 0);
	assert(strcmp(ButtonLayout_displayLabel("MENU+B", 1), "MENU+A") == 0);
	assert(strcmp(ButtonLayout_displayLabel("MENU+X", 1), "MENU+Y") == 0);
	assert(strcmp(ButtonLayout_displayLabel("MENU+Y", 1), "MENU+X") == 0);
	// everything else untouched, including the d-pad and shoulder labels
	const char* l1 = "L1";
	assert(ButtonLayout_displayLabel(l1, 1) == l1);
	const char* lr = "LEFT/RIGHT";
	assert(ButtonLayout_displayLabel(lr, 1) == lr);
	const char* start = "START";
	assert(ButtonLayout_displayLabel(start, 1) == start);
	// NULL and empty are safe
	assert(ButtonLayout_displayLabel(NULL, 1) == NULL);
	const char* empty = "";
	assert(ButtonLayout_displayLabel(empty, 1) == empty);
}

int main(void) {
	test_swap_face();
	test_display_label();
	printf("test_button_layout: OK\n");
	return 0;
}
