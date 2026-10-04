#ifndef TILES_H
#define TILES_H

#include "gameinfo_text.h"
#include "sdl.h"

// The main menu's Grid tiles (logo, icon, name and count) and the Carousel's console logos and count: a fixed
// off-white (#E0E0E0), not the theme or accent colours
#define TILE_MENU_GREY 0xE0
// The main menu's "N games" lines (Grid and Carousel, Consoles and Collections): a dimmer grey than the logo or name
#define TILE_COUNT_GREY 0x80
// A console without a bundled logo: this emblem over its name (Grid and Carousel)
#define TILE_UNKNOWN_CONSOLE_ICON "menu_icon_unknown.png"
// A tool without a mapped icon (Tiles_toolIcon misses on its name and its folder): this one
#define TILE_UNKNOWN_TOOL_ICON "menu_icon_tool_unknown.png"

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
	unsigned picture_gen;  // HomeArt_lastGen() of picture's lookup: the caller's cache key (Tiles_draw ignores it)
	const InfoSeg* info;   // lit game/title caption info (count-only segments), may be NULL
	int ninfo;
	float scale; // tile_w / 140: insets and text scale with the tile (never above 1)
	// The Grid's selected console or collection: "N games" in the accent (NULL or "" = no line). Only the lit look
	// draws it; a collection reserves its line either way.
	const char* count;
	// The Carousel's look (§8b.3): a tool's icon at 30% of the tile height and its name at 20 sp × tile_w / 340
	// (capped so "Achievements" fits); a lit game shows only its ring (the caption sits under the row instead).
	bool carousel;
	// Leave out the lit caption (the fade, the name and the info): the Grid caches a tile's lit look without it, so
	// the info arriving later doesn't recompose the tile, and draws it per frame with Tiles_drawCaption.
	bool no_caption;
} TileSpec;

// lit 0..1 crossfades plain → lit: GAME/TITLE get the 3 dp accent ring + caption; the Grid's LOGO/TOOL/COLLECTION the
// "Logo" look (a 1.5 dp outline at 70% accent, the content in the accent, the count); the Carousel's fill (the opaque
// accent, the content in its ink: white and black by default).
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
// The widest word of name (split on spaces and tabs; each word cut to 255 bytes) as measured in f (0 wide when f is
// NULL) into out. The Carousel/Backdrop collection slot and the Grid's name tile share it. The Grid's old copy split on
// spaces only and read the name through a 255-byte buffer: the same word for any name a FAT or exFAT card holds within
// 255 bytes (neither allows control characters, so no tabs; every ASCII name, at most 255 characters, qualifies).
void Tiles_longestWord(TTF_Font* f, const char* name, char* out, size_t size);
// A collection name's size (sp) in avail px: from start, Row_collNameSp on the longest word's width at start, then
// down in whole sp while that word still overflows, never below Row_collNameFloor(start, count_sp). A NULL font at a
// size stops the shrink there.
float Tiles_collNameSp(const char* name, float start, float count_sp, int avail);
// Word-wrapped text, each line centred on cx, the block's top at y: at most max_lines (the last one ellipsised; a
// word wider than max_w too). camel_split: a one-word name that doesn't fit breaks at its first lower→upper case
// boundary ("RetroAchievements" → "Retro" / "Achievements"). White tinted to grey level `grey` at `alpha`, with the
// dark shadow (black 60%, SCALE1(1) right and down) when `shadow`. Returns the block's height (lines × line
// height); a NULL dst only measures.
int Tiles_textBlock(SDL_Surface* dst, TTF_Font* f, const char* text, int cx, int y, int max_w, int max_lines,
					bool camel_split, Uint8 grey, Uint8 alpha, bool shadow);
// The same with each line in a line_h px box (the glyphs centred in it; 0 = the font's height): a CSS line height
// (Collections' 1.15). Returns lines × line_h.
int Tiles_textBlockStep(SDL_Surface* dst, TTF_Font* f, const char* text, int cx, int y, int max_w, int max_lines,
						bool camel_split, Uint8 grey, Uint8 alpha, bool shadow, int line_h);
// Tiles_textBlock left-aligned: every line's left edge at x (the game lists' side caption, §8f.4), opaque.
int Tiles_textBlockLeft(SDL_Surface* dst, TTF_Font* f, const char* text, int x, int y, int max_w, int max_lines,
						Uint8 grey, bool shadow);
// ...and right-aligned: every line's right edge at x (the side caption with Vertical alignment Right).
int Tiles_textBlockRight(SDL_Surface* dst, TTF_Font* f, const char* text, int x, int y, int max_w, int max_lines,
						 Uint8 grey, bool shadow);
// Free the cached shape masks, captions and scratch surface.
void Tiles_quit(void);

#endif // TILES_H
