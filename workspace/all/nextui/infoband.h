#ifndef INFOBAND_H
#define INFOBAND_H
#include "gameinfo_text.h"
#include "infoband_layout.h"
#include "sdl.h"
#include <stdbool.h>

// The band for the current List screen: arrows (packed, at layout->arrow_x, the rows' text start; 0 = 14 dp) + one
// line of segments, right-aligned.
// `layout` is the caller's list geometry (the one its rows use), so the band and the rows always agree.
// Drawn onto `layer` from one cached ARGB block (rebuilt only when text/arrows/size change).
void InfoBand_render(const InfoBandLayout* layout, const InfoSeg* segs, int nsegs, bool up, bool down, int layer);
// A plain grey text line ("24 games") with the same arrows: wraps InfoBand_render with one INFO_SEG_COUNT segment.
void InfoBand_renderText(const InfoBandLayout* layout, const char* text, bool up, bool down, int layer);
// The segment painter (the band, the Game Switcher, Home): one line of segments in `font` (font.small on List
// screens, 14 sp; Home's tiles use 11 sp) with the dark shadow, separators and the trophy (12 dp at font.small,
// scaled with the line height otherwise), fitted to max_w (the last segment is cut, then dropped). Its right edge
// at x (align_right) or its left edge at x; y is the top of a TTF_FontHeight(font) line. Returns the drawn width
// (0 = nothing).
int InfoBand_drawSegments(SDL_Surface* dst, const InfoSeg* segs, int n, int x, bool align_right, int y, int max_w,
						  TTF_Font* font);
// The width InfoBand_drawSegments would draw for the same arguments (0 = nothing): to centre a line.
int InfoBand_segmentsWidth(const InfoSeg* segs, int n, int max_w, TTF_Font* font);
void InfoBand_quit(void); // free the cached block
#endif
