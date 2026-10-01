#ifndef TILES_H
#define TILES_H

#include "gameinfo_text.h"
#include "sdl.h"

// One main-menu tile (Grid and Carousel), drawn into `dst` at px rect r.
typedef enum { TILE_LOGO,
			   TILE_TOOL,
			   TILE_COLLECTION,
			   TILE_GAME,
			   TILE_TITLE } TileKind;
typedef struct {
	TileKind kind;
	const char* name;	   // display name (logo fallback, tool/collection/title text, lit game caption)
	const char* logo_file; // TILE_LOGO: "menu_logo_<id>.png" or NULL (→ name)
	const char* icon_file; // TILE_TOOL: "menu_icon_*.png" or NULL (→ name only)
	SDL_Surface* picture;  // TILE_GAME: screenshot already cropped to the tile (owned by the caller's cache)
	const InfoSeg* info;   // lit game/title caption info (count-only segments), may be NULL
	int ninfo;
	float scale; // tile_w / 140: insets and text scale with the tile (never above 1)
	// The Carousel's look (§8b.3): a tool's icon at 30% of the tile height and its name at 20 sp × tile_w / 340
	// (capped so "Achievements" fits); a lit game shows only its ring (the caption sits under the row instead).
	bool carousel;
	// Leave out the lit caption (the fade, the name and the info): the Grid caches a tile's lit look without it, so
	// the info arriving later doesn't recompose the tile, and draws it per frame with Tiles_drawCaption.
	bool no_caption;
} TileSpec;

// lit 0..1 crossfades plain → lit (fill for LOGO/TOOL/COLLECTION; ring + caption for GAME/TITLE).
// Everything is blitted, so it honours dst's clip rect. The game ring sits 3 dp outside r.
void Tiles_draw(SDL_Surface* dst, SDL_Rect r, const TileSpec* t, float lit);
// Only the lit caption of a game or title tile (not the Carousel's) at r, at the lit amount: the cached caption
// surface blended over dst, which must be opaque under r (UI_blitBlendOpaque). Nothing for other kinds.
void Tiles_drawCaption(SDL_Surface* dst, SDL_Rect r, const TileSpec* t, float lit);
// The bundled icon for a tool name ("Settings", "Settings.pak", "Artwork-Manager", ...): the four bundled mappings,
// matched on letters and digits only, case-insensitive (a trailing ".pak" ignored); else NULL. The single mapping
// (Home and the tiles).
const char* Tiles_toolIcon(const char* pak_name);
// Break a one-word name at its first lowercase→uppercase boundary, in place ("RetroAchievements" → "Retro
// Achievements"). False (buf unchanged) when it has a space, no such boundary, or no room for the space.
bool Tiles_camelSplit(char* buf, size_t size);
// The largest size from sp_max down to sp_min (1 sp steps) at which the widest space-separated word of `text` fits
// max_w px (in the regular or SemiBold face); sp_min when none does (the caller then cuts per line). Names on tiles
// shrink before they lose letters (Home's tiles and the Grid's tool names).
float Tiles_fitWordsSp(const char* text, float sp_max, float sp_min, bool bold, int max_w);
// Word-wrapped text, each line centred on cx, the block's top at y: at most max_lines (the last one ellipsised; a
// word wider than max_w too). camel_split: a one-word name that doesn't fit breaks at its first lower→upper case
// boundary ("RetroAchievements" → "Retro" / "Achievements"). White tinted to grey level `grey` at `alpha`, with the
// dark shadow (black 60%, SCALE1(1) right and down) when `shadow`. Returns the block's height (lines × line
// height); a NULL dst only measures.
int Tiles_textBlock(SDL_Surface* dst, TTF_Font* f, const char* text, int cx, int y, int max_w, int max_lines,
					bool camel_split, Uint8 grey, Uint8 alpha, bool shadow);
// Free the cached shape masks, captions and scratch surface.
void Tiles_quit(void);

#endif // TILES_H
