#ifndef UI_LIST_H
#define UI_LIST_H

#include <stdbool.h>
#include <stdint.h>
#include "sdl.h"

// Scrolling text state for marquee animation
typedef struct {
	char text[512];						// Text to display
	int text_width;						// Full text width in pixels
	int max_width;						// Maximum display width
	uint32_t start_time;				// Animation start time
	bool needs_scroll;					// True if text is wider than max_width
	int scroll_offset;					// Current pixel offset for smooth scrolling
	bool use_gpu_scroll;				// True = use GPU layer (for lists), False = software (for player)
	int last_x, last_y;					// Last render position (for animate-only mode)
	TTF_Font* last_font;				// Last font used (for animate-only mode)
	SDL_Color last_color;				// Last color used (for animate-only mode)
	SDL_Surface* cached_scroll_surface; // Cached surface for GPU scroll (no bg)
	bool scroll_active;					// True once GPU scroll has actually started (after delay)
	bool rtl;							// True if the text's base direction is RTL (Arabic) — reverses scroll
} ScrollTextState;

void ScrollText_reset(ScrollTextState* state, const char* text,
					  TTF_Font* font, int max_width, bool use_gpu);
// Free the cached scroll surface and return the state to inactive/empty.
// Call when the scrolled row leaves the screen (screen switch, list exit).
void ScrollText_clear(ScrollTextState* state);
bool ScrollText_isScrolling(ScrollTextState* state);
bool ScrollText_needsRender(ScrollTextState* state);
void ScrollText_activateAfterDelay(ScrollTextState* state);
void ScrollText_animateOnly(ScrollTextState* state);
void ScrollText_render(ScrollTextState* state, TTF_Font* font,
					   SDL_Color color, SDL_Surface* screen, int x, int y);

// Unified update: checks for text change, resets if needed, and renders
// use_gpu: true for lists (GPU layer with pill bg), false for player (software, no bg)
void ScrollText_update(ScrollTextState* state, const char* text, TTF_Font* font,
					   int max_width, SDL_Color color, SDL_Surface* screen, int x, int y, bool use_gpu);

// GPU scroll without background (for player title)
// Uses PLAT_drawOnLayer to render to GPU layer without pill background
void ScrollText_renderGPU_NoBg(ScrollTextState* state, TTF_Font* font,
							   SDL_Color color, int x, int y);

// ---- List Layout ----

typedef struct {
	int list_y;			// Y where list starts
	int list_h;			// Height available for list
	int item_h;			// Height per item
	int items_per_page; // Visible item count
	int max_width;		// Max content width
} ListLayout;

ListLayout UI_calcListLayout(SDL_Surface* screen);

// ---- Pill Rendering (stateless) ----

typedef struct {
	int pill_width;
	int text_x;
	int text_y;
} ListItemPos;

int UI_calcListPillWidth(TTF_Font* font, const char* text, char* truncated,
						 int max_width, int prefix_width);
void UI_drawListItemBg(SDL_Surface* dst, SDL_Rect* rect, bool selected);
SDL_Color UI_getListTextColor(bool selected);

// Render a list item's pill background and calculate text position
// Combines: Fonts_calcListPillWidth + Fonts_drawListItemBg + text position calculation
// prefix_width: extra width to account for (e.g., checkbox, indicator)
ListItemPos UI_renderListItemPill(SDL_Surface* screen, ListLayout* layout,
								  TTF_Font* font, const char* text,
								  char* truncated, int y, bool selected,
								  int prefix_width);

void UI_renderListItemText(SDL_Surface* screen, ScrollTextState* scroll_state,
						   const char* text, TTF_Font* font,
						   int text_x, int text_y, int max_text_width,
						   bool selected);

// ---- Badged Pill Rendering ----

// Position information returned by render_list_item_pill_badged
typedef struct {
	int pill_width;		// Width of the title (inner) pill
	int text_x;			// X position for title text
	int text_y;			// Y position for title text (row 1)
	int subtitle_x;		// X position for subtitle text (row 2)
	int subtitle_y;		// Y position for subtitle text (row 2)
	int badge_x;		// X position for badge content start
	int badge_y;		// Y position for badge content (centered)
	int total_width;	// Total width of title pill + badge area
	int text_max_width; // Max width for text content
} ListItemBadgedPos;

// Render a two-row list item pill with optional right-side badge area.
// Item height is 1.5x PILL_SIZE. Title (title_font) + subtitle (subtitle_font).
// When badge_width > 0 and selected: THEME_COLOR2 outer capsule + THEME_COLOR1 inner.
// When badge_width == 0: single THEME_COLOR1 capsule.
// Caller renders badge content at badge_x, badge_y.
ListItemBadgedPos UI_renderListItemPillBadged(
	SDL_Surface* screen, ListLayout* layout,
	TTF_Font* title_font, TTF_Font* subtitle_font, TTF_Font* badge_font,
	const char* text, const char* subtitle, char* truncated,
	int y, bool selected, int badge_width, int extra_subtitle_width);

// ---- Settings Page Component ----

typedef struct {
	const char* label; // Left-side text
	const char* value; // Right-side text (NULL for none)
	int swatch;		   // Color swatch (-1 for none)
	int cycleable;	   // Show "< >" arrows when selected
	const char* desc;  // Description shown when item is selected
	void (*custom_draw)(SDL_Surface* screen, void* ctx,
						int x, int y, int w, int h, int selected);
	void* custom_draw_ctx;
} UISettingsItem;

// Render a compact settings page (9 rows: 8 items + 1 description)
// Handles layout calculation, scrolling, item rendering, scroll indicators,
// status message, and description text.
void UI_renderSettingsPage(SDL_Surface* screen, ListLayout* layout,
						   UISettingsItem* items, int count,
						   int selected, int* scroll,
						   const char* status_msg);

// ---- Settings Row Rendering ----

// Render a settings-style row: label (left) + value (right)
// - selected: 2-layer pill (THEME_COLOR2 full-width + THEME_COLOR1 label-width),
//   value with "< >" arrows
// - unselected: no background, label + value text only
// - swatch_color: if non-negative, draws a color swatch square next to the value
// Returns the x position where value rendering ended (left edge of value area)
int UI_renderSettingsRow(SDL_Surface* screen, ListLayout* layout,
						 const char* label, const char* value,
						 int y, bool selected, int swatch_color);

// ---- Scroll Helpers ----

void UI_adjustListScroll(int selected, int* scroll, int items_per_page);
void UI_renderScrollIndicators(SDL_Surface* screen, int scroll,
							   int items_per_page, int total_count);

// ---- Pill Animation (non-threaded, for main-loop driven apps) ----

typedef struct {
	int current_y;
	int target_y;
	int start_y;
	// Width glides alongside y: a snap to the new row's width while y is still
	// travelling reads as a jump, not a glide (short -> long titles especially).
	int current_w;
	int target_w;
	int start_w;
	uint32_t start_time; // SDL_GetTicks() when the current glide began
	bool active;
} PillAnimState;

void UI_pillAnimSetTarget(PillAnimState* state, int target_y, int target_w, bool animate);
int UI_pillAnimTick(PillAnimState* state);
bool UI_pillAnimIsActive(PillAnimState* state);

// ---- List Selection Glide ----
// Core wiring for the animated selection pill (PILL_ANIM_MS smoothstep,
// width morphs with y). Render-only: the caller keeps owning selection and
// input. New list surfaces should use the ListView widget (ui_listview.h),
// which drives this core; direct use remains for the game list until it
// migrates. Per frame, BEFORE the row loop: call UI_listGlideDrawAtY with
// the selected row's target y and the band, then render every row with
// selected=false and tint its text with UI_listGlideRowSelected(&frame,
// row_y, item_h). Dirty-flag main loops must keep redrawing while
// UI_listGlideActive(&glide).

typedef struct {
	PillAnimState anim;	 // y + width glide state
	const void* list_id; // content identity last drawn (NULL = never)
	int prev_target_y;	 // last settled target (retarget detection)
} ListGlide;			 // zero-init = valid initial state

typedef struct {
	int pill_y;		// interpolated pill top for this frame
	bool animating; // true while mid-glide
} ListGlideFrame;

// Core (irregular row geometry, e.g. lists with header rows): glide toward
// target_y, clipped to the band [band_y, band_y+band_h). Snaps when
// list_id changes or allow_anim is false. pill_w <= 0 skips the draw.
ListGlideFrame UI_listGlideDrawAtY(ListGlide* g, SDL_Surface* screen,
								   const void* list_id, int target_y,
								   int band_y, int band_h,
								   int item_h, int pill_w, bool allow_anim);
// True when the pill covers the majority of the row at row_y — draw that
// row's text in the selected colour exactly then.
bool UI_listGlideRowSelected(const ListGlideFrame* f, int row_y, int item_h);
// For dirty-flag main loops: keep redrawing while true.
bool UI_listGlideActive(ListGlide* g);

// ---- Rich Pill Rendering ----

// Position information returned by UI_renderListItemPillRich
typedef struct {
	int pill_width;				// Width of the rendered pill
	int title_x, title_y;		// Row 1 position (medium font)
	int subtitle_x, subtitle_y; // Row 2 position (small font)
	int image_x, image_y;		// Image position (top-left corner)
	int image_size;				// Image width & height (square)
	int text_max_width;			// Max width for text (for scrolling)
} ListItemRichPos;

// Render a 2-row list item pill with image area on the left
// Height is 1.5x PILL_SIZE. Image is square, vertically centered.
// Row 1: title (medium font), Row 2: subtitle (small font)
// Caller renders image at image_x/image_y and text via UI_renderListItemText()
ListItemRichPos UI_renderListItemPillRich(SDL_Surface* screen, ListLayout* layout,
										  const char* title, const char* subtitle,
										  char* truncated,
										  int y, bool selected, bool has_image,
										  int extra_subtitle_width);

// ---- Menu Item Pill Rendering ----

// Position information returned by UI_renderMenuItemPill
typedef struct {
	int pill_width; // Width of the rendered pill
	int text_x;		// X position for text (after padding)
	int text_y;		// Y position for text (vertically centered in pill)
	int item_y;		// Y position of this menu item
} MenuItemPos;

// Render a menu item's pill background and calculate text position
// Menu items use PILL_SIZE height. index: menu item index (0-based)
// prefix_width: extra width to account for (e.g., icon)
MenuItemPos UI_renderMenuItemPill(SDL_Surface* screen, ListLayout* layout,
								  const char* text, char* truncated,
								  int index, bool selected, int prefix_width);

// ---- Rounded Rectangle Background ----

// Render a filled rounded rectangle background
// Works at any height (unlike pill asset which requires PILL_SIZE)
void UI_renderRoundedRectBg(SDL_Surface* screen, int x, int y, int w, int h, uint32_t color);

#endif // UI_LIST_H
