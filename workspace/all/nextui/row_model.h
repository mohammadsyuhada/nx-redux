#ifndef ROW_MODEL_H
#define ROW_MODEL_H

#include <stdbool.h>

// row_model.h — Carousel/Backdrop geometry (dp) and curves. SDL-free.
typedef enum { ROW_CAROUSEL,
			   ROW_BACKDROP_BOX,
			   ROW_BACKDROP_LOGO,
			   ROW_BACKDROP_TOOL } RowKind;
typedef struct {
	float item_w, item_h, scale, gap, f;
} RowSizes;
float Row_factor(float body_w, float body_h);			   // min(1, w/960, h/358)
RowSizes Row_sizes(RowKind k, float body_w, float body_h); // includes the Backdrop box-slot growth
typedef struct {
	float dx, scale, darken, alpha;
	bool visible;
} RowItem; // dx: centre offset from the row centre
// The Backdrop box slot (k == ROW_BACKDROP_BOX; other kinds untouched) gives way to its caption: when room + row +
// room + caption_gap + caption_h is taller than body_h, the slot shrinks (keeping its shape) until the block fits,
// never below the spec-scaled size (a block that still doesn't fit clamps under the header, see Row_top).
void Row_fitBoxSlot(RowSizes* s, RowKind k, float body_h, float room, float caption_gap, float caption_h);
RowItem Row_item(const RowSizes* s, RowKind k, float index, float pos);
void Row_visibleRange(int n, float pos, int* first, int* last); // items with d < 4, clamped; last < first when n == 0
// Block placement: returns the row's top y (dp). caption_h 0 = no caption reserved. room = ring/shadow room under/over.
float Row_top(float body_top, float body_h, float row_h, float room, float caption_gap, float caption_h);
float Row_shade(float y_frac); // the monotone cubic through the six keys
// Separable box blur of an 8-bit alpha buffer, `passes` times with radius r (in px). In place via tmp (same size).
void Row_boxBlurAlpha(unsigned char* a, unsigned char* tmp, int w, int h, int r, int passes);

#endif
