#include "homeart_model.h"

#include <stdbool.h>
#include <stdlib.h>

#define BAR_MAX 4 // a channel at or under this counts as black

static bool isBlack(unsigned p) {
	return ((p >> 16) & 0xFF) <= BAR_MAX && ((p >> 8) & 0xFF) <= BAR_MAX && (p & 0xFF) <= BAR_MAX;
}

static bool rowBlack(const unsigned* px, int w, int pitch, int y) {
	const unsigned* row = px + (long)y * pitch;
	for (int x = 0; x < w; x++)
		if (!isBlack(row[x]))
			return false;
	return true;
}

static bool colBlack(const unsigned* px, int h, int pitch, int x) {
	for (int y = 0; y < h; y++)
		if (!isBlack(px[(long)y * pitch + x]))
			return false;
	return true;
}

// Accept a lead/trail bar pair along an axis of `len` px: symmetric within max(2, 1%) and leaving at least a quarter.
static bool barsAccepted(int lead, int trail, int len) {
	int tol = len / 100 > 2 ? len / 100 : 2;
	return abs(lead - trail) <= tol && len - lead - trail >= len / 4;
}

HomeArtRect HomeArt_trimLetterbox(const unsigned* px, int w, int h, int pitch_words) {
	HomeArtRect r = {0, 0, w, h};
	if (!px || w <= 0 || h <= 0)
		return r;

	int top = 0, bottom = 0;
	while (top < h && rowBlack(px, w, pitch_words, top))
		top++;
	while (bottom < h - top && rowBlack(px, w, pitch_words, h - 1 - bottom))
		bottom++;
	if (top < h && (top || bottom) && barsAccepted(top, bottom, h)) {
		r.y = top;
		r.h = h - top - bottom;
	}

	int left = 0, right = 0;
	while (left < w && colBlack(px, h, pitch_words, left))
		left++;
	while (right < w - left && colBlack(px, h, pitch_words, w - 1 - right))
		right++;
	if (left < w && (left || right) && barsAccepted(left, right, w)) {
		r.x = left;
		r.w = w - left - right;
	}
	return r;
}

bool HomeArt_isBlankFrame(const unsigned* px, HomeArtRect keep, int pitch_words) {
	if (!px || keep.w <= 0 || keep.h <= 0)
		return true;
	long total = (long)keep.w * keep.h;
	long allowed = total / 50; // up to 2% lit
	long lit = 0;
	for (int y = keep.y; y < keep.y + keep.h; y++) {
		const unsigned* row = px + (long)y * pitch_words;
		for (int x = keep.x; x < keep.x + keep.w; x++)
			if (!isBlack(row[x]) && ++lit > allowed)
				return false;
	}
	return true;
}
