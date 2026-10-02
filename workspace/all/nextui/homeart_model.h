#ifndef HOMEART_MODEL_H
#define HOMEART_MODEL_H

// SDL-free picture math for the Home tab (host-tested).

#include <stdbool.h>

// pixels as 0xAARRGGBB words, row-major
typedef struct {
	int x, y, w, h;
} HomeArtRect;

// Trim symmetric pure-black letterbox bars (every channel <= 4). Rows trimmed only when the top and bottom bars match
// within max(2 px, 1% of the height); columns likewise. Nothing trimmed when the result would be under a quarter of
// the frame in either axis. Returns the kept rect (the full frame when nothing is trimmed).
HomeArtRect HomeArt_trimLetterbox(const unsigned* px, int w, int h, int pitch_words);

// Whether a frame shows nothing: at least 98% of the pixels in `keep` (the trimmed rect, inside the frame) are black,
// every channel <= 4. An empty rect or no pixels count as blank. A blank resume frame (a black save-state preview) is
// treated as missing, so Continue falls back to the screenshot.
bool HomeArt_isBlankFrame(const unsigned* px, HomeArtRect keep, int pitch_words);

// Where a crop-to-fill frames a w×h picture (0 = its top, 1 = its bottom): a picture taller than wide (a Nintendo DS
// screenshot stacks its two screens, 256×384) shows its top, so the top screen fills the tile; any other keeps dflt.
float HomeArt_frameY(int w, int h, float dflt);

#endif // HOMEART_MODEL_H
