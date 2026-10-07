#ifndef ROWVIEW_SHARED_H
#define ROWVIEW_SHARED_H

// rowview.c's item surfaces and the main-menu count line, shared with the Vertical orientation (stackview.c): the
// same frameless slots (SP8: Consoles' logo slot, Collections' names, Tools' icon and name), the game lists' items
// (the Carousel's tiles, the Backdrop's box art and placeholder box), all cached the same way (one surface per rest
// size, keyed by size and item), the same "N games" line with its glide and fade, and the game caption beside a
// stack. Not for any other module.

#include "api.h"
#include "config.h"
#include "defines.h"
#include "sdl.h"
#include "row_model.h"
#include "tiles.h"
#include "types.h"
#include "view_common.h" // the timing and units both views use
#include <stdbool.h>

// Prefetch (built between frames, never forcing one): it may run through a single step's slide once the slide is this
// far through, and stops at the caller's deadline (SDL ticks, wrap-safe).
#define PREFETCH_SLIDE_SHARE 0.6f
static inline bool RowView_pastDeadline(Uint32 deadline) {
	return (Sint32)(SDL_GetTicks() - deadline) >= 0;
}
// A job ahead is only started when what it usually costs still fits before the deadline: a caption (its text, ~5-8 ms
// on the Brick) or a texture upload begun with 2 ms left ran past the next frame's start, a late frame. Costs are
// learned per kind (RowView_jobDone with the job's start ticks; a cache hit, under a ms, teaches nothing).
typedef enum { PF_JOB_CAPTION,
			   PF_JOB_ITEM,
			   PF_JOB_COUNT } PrefetchJob;
bool RowView_jobFits(Uint32 deadline, PrefetchJob job);
void RowView_jobDone(PrefetchJob job, Uint32 t0);

typedef enum { CAP_NONE,
			   CAP_GAME } CapKind;

// Where the items sit and how big they are (px), for one frame.
typedef struct {
	RowKind kind;
	CapKind cap;
	RowSizes sz;		// the slot (dp) and its neighbour scale
	int cx, cy;			// the selection's centre (px): the row's centre, or a stack's selection
	int full_w, full_h; // the centre item at rest (px)
	int side_w, side_h; // a neighbour at rest (px)
	int cap_y, cap_h;	// the caption block (px; cap_h 0 = none reserved)
	int cap_draw_h;		// the caption's drawn height: cap_h, less what would fall under the hint bar
	int body_cy;		// the body's vertical centre (px): a game Carousel's neighbours sit on it
	int cap_gap;		// the selection's picture to its caption (px)
	float k;			// a Backdrop slot's content scale: min(1, slot_w / spec slot_w)
	bool vertical;		// a Vertical stack's slots (stackview.c): their own cache keys, and a logo-less console's
						// count under at least a half-slot "logo" (§8f.3)
} RowGeo;


// The selected item as drawn this frame (px offsets from g->cx / g->cy), for the count line on it.
typedef struct {
	int dx, dy;
	float scale, alpha;
	bool visible;
} RowPlace;

// The spec slot widths a slot's content scale k divides by (min(1, slot_w / spec)).
#define ROWVIEW_LOGO_SLOT_W_SPEC 330.0f
#define ROWVIEW_TOOL_SLOT_W_SPEC 150.0f
#define ROWVIEW_COLL_SLOT_W_SPEC 300.0f	   // Horizontal Collections: twice the tool slot, the text scale unchanged
#define ROWVIEW_TOOLROW_SLOT_W_SPEC 240.0f // Horizontal Tools: the wider slot, its content scaled by the factor below
#define ROWVIEW_TOOLROW_CONTENT 1.3f

// Tile kinds per index of the current list (worked out on first sight, kept per list and tab generation).
void RowView_syncKinds(int n);
TileKind RowView_kindFor(int index, Entry* e);
// A frameless slot item (Consoles, Collections, Tools) at a rest size: the selected one, or a neighbour (side). Cached.
SDL_Surface* RowView_slotItem(const RowGeo* g, Entry* e, TileKind kind, bool side);
// s centred on (cx, cy), scaled by factor (1:1 at 1), at alpha a, onto an opaque dst.
void RowView_blitItem(SDL_Surface* dst, SDL_Surface* s, int cx, int cy, float factor, Uint8 a);
// The main-menu "N games" line for the selection `sel` drawn at `at` (Consoles: under the selection's logo as drawn,
// gliding; Collections: on the item, fading in). Nothing for other kinds. `snap`: a new list, tab or screen.
void RowView_drawCount(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int sel, const RowPlace* at,
					   bool snap);

// A game-list item centred on (cx, cy): the Carousel's tile (g->kind ROW_CAROUSEL: the 14 dp corners, the screenshot
// or a title tile, the 3 dp accent ring as it nears the selection, darkened by `darken` toward the black ground) or
// the Backdrop's box art or placeholder box with its shadow (at `alpha`), a frameless slot otherwise. scale goes from
// 1 (the selected size) to g->sz.scale (a neighbour's); d is the item's distance from the position (steps).
void RowView_drawGameItem(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy, float scale,
						  float alpha, float darken, float d);
// A game list's Carousel or Backdrop (either orientation) draws this frame as GPU sprites: one with items, and no
// context menu open (it draws over the body on the screen). Its picture, items, caption and edge fade are then sprites
// and the screen's body holds nothing that moves.
bool RowView_gameSprites(void);
// The picture is busy: its screenshot crossfade or B's exit fade is running. Prefetch waits for it (a build would
// stall the fade's frames), as the row's own does.
bool RowView_pictureBusy(void);
// Whether prefetch must wait: B's exit fade, or the picture's crossfade when it runs on the CPU (in sprite mode it is
// the GPU's, and a held D-pad's steps keep it running).
bool RowView_prefetchBlocked(void);
// Make the game caption beside a stack ahead (stackview's prefetch), as RowView_drawSideCaption would show it, then
// (sprite mode) its texture if that still fits before the deadline: false when it didn't (stop for this frame).
bool RowView_prefetchSideCaption(const RowGeo* g, Entry* e, TileKind kind, int w, int body_h, bool right,
								 Uint32 deadline);
// The frame showed an older caption of the selection (its info rows since changed, sprite mode): prefetch makes the
// selection's own first, then RowView_captionRefreshed (one more frame shows it).
bool RowView_captionStale(void);
void RowView_captionRefreshed(void);
// Build one item ahead at the selected size (side false; `lit`: a Carousel tile's lit look) or a neighbour's (a cache
// hit builds nothing).
void RowView_prefetchItem(const RowGeo* g, Entry* e, TileKind kind, bool side, bool lit);
// The game caption beside a stack (§8f.4): the name (16 sp; 2 lines), the time row and the
// trophy row (14 sp; a long Next on its own lines, caption_fit.h), 3 dp apart, left-aligned from x in w px
// (right: right-aligned to x + w, Vertical alignment Right), centred on cy and kept inside the body (body_top, body_h).
// Backdrop's (g->kind ROW_BACKDROP_BOX) keeps the text shadow. One cached surface.
void RowView_drawSideCaption(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int x, int w, int cy,
							 int body_top, int body_h, bool right);

// The Consoles tab's controller art (Layouts > Controller): console e's pad fitted in box_w x box_h px, centred on
// (cx, cy), scaled with its item, at Pad_alpha(d) (0.6 focused, gone a step away). Nothing for other kinds, a console
// without a pad, or the setting off. Drawn just before the item's logo.
void RowView_drawPad(SDL_Surface* screen, Entry* e, TileKind kind, int box_w, int box_h, int cx, int cy, float scale,
					 float d);
// Console e's pad id (Pad_idForFolder), or NULL; for a stack's spacing.
const char* RowView_padId(Entry* e, TileKind kind);
// Load ahead (a cache hit loads nothing) the pad RowView_drawPad would draw for console e in box_w x box_h px.
// Consoles: queue the logos and controllers around sel on the art loader (artloader.h), nearest first.
// Consoles in sprite mode: its logo as a GPU sprite (the selection's crisp one once decoded, else the half/quarter level
// scaled by the GPU). False when it is not that case (draw as before).
bool RowView_drawConsoleLogo(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy, float scale,
							 Uint8 a, bool selected);
void RowView_warmConsoleArt(int slot_w, int slot_h, int pad_w, int pad_h, int sel);
void RowView_prefetchPad(Entry* e, TileKind kind, int box_w, int box_h);
// The Consoles "N games" line's height (px) for this geometry's text scale.
int RowView_countLineH(const RowGeo* g);

// A Vertical stack's Consoles "N games" on its own item e (centre cx, cy px, live scale, d steps from the position):
// 8 dp under the item's logo as drawn, scaled with it, fading 1 − d (Stack_countOn). Nothing for other kinds. The
// stack draws it on the items within a step of the position, instead of RowView_drawCount's fixed-slot glide.
void RowView_drawItemCount(SDL_Surface* screen, const RowGeo* g, Entry* e, TileKind kind, int cx, int cy, float scale,
						   float d);

#endif
