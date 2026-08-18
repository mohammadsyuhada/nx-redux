#include <stdio.h>
#include <string.h>
#include "ui_list.h"
#include "api.h"
#include "defines.h"
#include "config.h"
#include "ui_draw.h"
#include "text_shape.h"

// Scroll gap for software scrolling
#define SCROLL_GAP 30

// Delay before scrolling starts (ms) - show static text first
#define SCROLL_START_DELAY 1000

// ============================================
// Scroll Text (marquee animation)
// ============================================

void ScrollText_reset(ScrollTextState* state, const char* text, TTF_Font* font, int max_width, bool use_gpu) {
	GFX_clearLayers(LAYER_SCROLLTEXT);
	// The GPU marquee keeps its offset in platform statics that outlive this
	// state struct. Reset them here, not just in ScrollText_render's
	// activation branch: idle-driven hosts activate via
	// ScrollText_activateAfterDelay and never take that branch, so a stale
	// offset from the previous item would make the new marquee start mid-text.
	GFX_resetScrollText();

	if (state->cached_scroll_surface) {
		SDL_FreeSurface(state->cached_scroll_surface);
		state->cached_scroll_surface = NULL;
	}

	strncpy(state->text, text, sizeof(state->text) - 1);
	state->text[sizeof(state->text) - 1] = '\0';
	state->rtl = TextShape_baseIsRTL(state->text);
	int text_h = 0;
	GFX_measureText(font, state->text, &state->text_width, &text_h);
	state->max_width = max_width;
	state->start_time = SDL_GetTicks();
	// RTL starts right-aligned (window at the text's right edge); LTR at 0.
	state->scroll_offset = (state->rtl && state->text_width > max_width)
							   ? state->text_width - max_width
							   : 0;
	state->use_gpu_scroll = use_gpu;
	state->scroll_active = false;
	state->needs_scroll = false;

	if ((state->text_width > max_width) && use_gpu) {
		int padding = SCALE1(SCROLL_GAP);
		int total_width = state->text_width * 2 + padding;
		int height = TTF_FontHeight(font);

		state->cached_scroll_surface = SDL_CreateRGBSurfaceWithFormat(0,
																	  total_width, height, 32, SDL_PIXELFORMAT_ARGB8888);

		if (state->cached_scroll_surface) {
			SDL_FillRect(state->cached_scroll_surface, NULL, 0);

			SDL_Color white = {255, 255, 255, 255};
			SDL_Surface* text_surf = GFX_renderText(font, state->text, white);
			if (text_surf) {
				SDL_SetSurfaceBlendMode(text_surf, SDL_BLENDMODE_NONE);
				SDL_BlitSurface(text_surf, NULL, state->cached_scroll_surface, &(SDL_Rect){0, 0, 0, 0});
				SDL_BlitSurface(text_surf, NULL, state->cached_scroll_surface, &(SDL_Rect){state->text_width + padding, 0, 0, 0});
				SDL_FreeSurface(text_surf);
			}
		}
	}
}

void ScrollText_clear(ScrollTextState* state) {
	if (state->cached_scroll_surface) {
		SDL_FreeSurface(state->cached_scroll_surface);
		state->cached_scroll_surface = NULL;
	}
	state->text[0] = '\0';
	state->needs_scroll = false;
	state->scroll_active = false;
}

bool ScrollText_isScrolling(ScrollTextState* state) {
	return state->needs_scroll;
}

bool ScrollText_needsRender(ScrollTextState* state) {
	return state->text[0] && state->text_width > state->max_width && !state->needs_scroll;
}

void ScrollText_activateAfterDelay(ScrollTextState* state) {
	if (!state->needs_scroll && state->text_width > state->max_width &&
		SDL_GetTicks() - state->start_time >= SCROLL_START_DELAY) {
		state->needs_scroll = true;
	}
}

void ScrollText_animateOnly(ScrollTextState* state) {
	if (!state->text[0] || !state->needs_scroll || !state->use_gpu_scroll)
		return;
	if (!state->last_font)
		return;

	GFX_clearLayers(LAYER_SCROLLTEXT);
	GFX_scrollTextTexture(
		state->last_font,
		state->text,
		state->last_x, state->last_y,
		state->max_width,
		TTF_FontHeight(state->last_font),
		state->last_color,
		1.0f,
		state->rtl,
		NULL);
}

void ScrollText_render(ScrollTextState* state, TTF_Font* font, SDL_Color color,
					   SDL_Surface* screen, int x, int y) {
	if (!state->text[0])
		return;

	state->last_x = x;
	state->last_y = y;
	state->last_font = font;
	state->last_color = color;

	if (!state->needs_scroll && state->text_width > state->max_width &&
		SDL_GetTicks() - state->start_time >= SCROLL_START_DELAY) {
		if (state->use_gpu_scroll && !state->scroll_active) {
			GFX_resetScrollText();
			state->scroll_active = true;
		} else {
			state->needs_scroll = true;
		}
	}

	if (!state->needs_scroll) {
		GFX_clearLayers(LAYER_SCROLLTEXT);
		SDL_Surface* surf = GFX_renderText(font, state->text, color);
		if (surf) {
			// RTL overflow (pre-scroll hold): show the right edge (the start of
			// the Arabic) rather than the left edge.
			int src_x = (state->rtl && surf->w > state->max_width) ? surf->w - state->max_width : 0;
			SDL_Rect src = {src_x, 0, surf->w > state->max_width ? state->max_width : surf->w, surf->h};
			SDL_BlitSurface(surf, &src, screen, &(SDL_Rect){x, y, 0, 0});
			SDL_FreeSurface(surf);
		}
		return;
	}

	if (state->use_gpu_scroll) {
		GFX_clearLayers(LAYER_SCROLLTEXT);
		GFX_scrollTextTexture(
			font,
			state->text,
			x, y,
			state->max_width,
			TTF_FontHeight(font),
			color,
			1.0f,
			state->rtl,
			NULL);
	} else {
		GFX_clearLayers(LAYER_SCROLLTEXT);

		SDL_Surface* single_surf = GFX_renderText(font, state->text, color);
		if (!single_surf)
			return;

		SDL_Surface* full_surf = SDL_CreateRGBSurfaceWithFormat(0,
																state->text_width * 2 + SCROLL_GAP, single_surf->h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!full_surf) {
			SDL_FreeSurface(single_surf);
			return;
		}

		SDL_FillRect(full_surf, NULL, 0);
		SDL_SetSurfaceBlendMode(single_surf, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(single_surf, NULL, full_surf, &(SDL_Rect){0, 0, 0, 0});
		SDL_BlitSurface(single_surf, NULL, full_surf, &(SDL_Rect){state->text_width + SCROLL_GAP, 0, 0, 0});
		SDL_FreeSurface(single_surf);

		if (state->rtl) {
			state->scroll_offset -= 2; // window moves left -> text scrolls right
			if (state->scroll_offset < 0)
				state->scroll_offset += state->text_width + SCROLL_GAP;
		} else {
			state->scroll_offset += 2;
			if (state->scroll_offset >= state->text_width + SCROLL_GAP)
				state->scroll_offset = 0;
		}

		SDL_SetSurfaceBlendMode(full_surf, SDL_BLENDMODE_BLEND);
		SDL_Rect src = {state->scroll_offset, 0, state->max_width, full_surf->h};
		SDL_Rect dst = {x, y, 0, 0};
		SDL_BlitSurface(full_surf, &src, screen, &dst);
		SDL_FreeSurface(full_surf);
	}
}

void ScrollText_update(ScrollTextState* state, const char* text, TTF_Font* font,
					   int max_width, SDL_Color color, SDL_Surface* screen, int x, int y, bool use_gpu) {
	// Reset on width change too, not just text change: the async thumbnail
	// arriving narrows the row mid-selection, and a stale max_width leaves
	// the scroll band wider than the pill it sits on (and the needs-scroll
	// decision measured against the wrong width).
	if (strcmp(state->text, text) != 0 || state->max_width != max_width) {
		ScrollText_reset(state, text, font, max_width, use_gpu);
	}
	ScrollText_render(state, font, color, screen, x, y);
}

// ============================================
// List Layout
// ============================================

ListLayout UI_calcListLayout(SDL_Surface* screen) {
	int hw = screen->w;
	int hh = screen->h;

	ListLayout layout;
	layout.list_y = SCALE1(PADDING + PILL_SIZE) + 10;
	layout.list_h = hh - layout.list_y - SCALE1(PADDING + BUTTON_SIZE + BUTTON_MARGIN);
	layout.item_h = SCALE1(PILL_SIZE);
	layout.items_per_page = layout.list_h / layout.item_h;
	layout.max_width = hw - SCALE1(PADDING * 2);

	return layout;
}

// ============================================
// Pill Rendering (stateless)
// ============================================

int UI_calcListPillWidth(TTF_Font* font, const char* text, char* truncated, int max_width, int prefix_width) {
	int available_width = max_width - prefix_width;
	int padding = SCALE1(BUTTON_PADDING * 2);

	int raw_text_w, raw_text_h;
	GFX_measureText(font, text, &raw_text_w, &raw_text_h);

	if (raw_text_w + padding > available_width) {
		GFX_truncateText(font, text, truncated, available_width, padding);
		return max_width;
	}

	strncpy(truncated, text, 255);
	truncated[255] = '\0';
	return MIN(max_width, prefix_width + raw_text_w + padding);
}

void UI_drawListItemBg(SDL_Surface* dst, SDL_Rect* rect, bool selected) {
	if (selected) {
		GFX_blitPillColor(ASSET_WHITE_PILL, dst, rect, THEME_COLOR1, RGB_WHITE);
	}
}

SDL_Color UI_getListTextColor(bool selected) {
	return selected ? uintToColour(THEME_COLOR5_255) : uintToColour(THEME_COLOR4_255);
}

ListItemPos UI_renderListItemPill(SDL_Surface* screen, ListLayout* layout,
								  TTF_Font* font, const char* text,
								  char* truncated, int y, bool selected,
								  int prefix_width) {
	ListItemPos pos;

	pos.pill_width = UI_calcListPillWidth(font, text, truncated, layout->max_width, prefix_width);

	SDL_Rect pill_rect = {SCALE1(PADDING), y, pos.pill_width, layout->item_h};
	UI_drawListItemBg(screen, &pill_rect, selected);

	pos.text_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
	pos.text_y = y + (layout->item_h - TTF_FontHeight(font)) / 2;

	return pos;
}

void UI_renderListItemText(SDL_Surface* screen, ScrollTextState* scroll_state,
						   const char* text, TTF_Font* font,
						   int text_x, int text_y, int max_text_width,
						   bool selected) {
	SDL_Color text_color = UI_getListTextColor(selected);

	SDL_Rect old_clip;
	SDL_GetClipRect(screen, &old_clip);
	SDL_Rect clip = {text_x, text_y, max_text_width, TTF_FontHeight(font)};
	if (old_clip.w > 0 && old_clip.h > 0) {
		int left = clip.x > old_clip.x ? clip.x : old_clip.x;
		int top = clip.y > old_clip.y ? clip.y : old_clip.y;
		int right = (clip.x + clip.w) < (old_clip.x + old_clip.w) ? (clip.x + clip.w) : (old_clip.x + old_clip.w);
		int bottom = (clip.y + clip.h) < (old_clip.y + old_clip.h) ? (clip.y + clip.h) : (old_clip.y + old_clip.h);
		if (right > left && bottom > top) {
			clip = (SDL_Rect){left, top, right - left, bottom - top};
		} else {
			return;
		}
	}
	SDL_SetClipRect(screen, &clip);

	if (selected && scroll_state) {
		// Logical text + primary font: the marquee's own renders go through the
		// Arabic-aware GFX_renderText/measureText (single shaping point), and
		// ScrollText picks the RTL scroll direction from the text itself.
		ScrollText_update(scroll_state, text, font, max_text_width,
						  text_color, screen, text_x, text_y, true);
	} else {
		// Cached surface (owned by the cache — do NOT free). Falls back to a
		// one-off render+free only for oversized strings the cache rejects.
		SDL_Surface* text_surf = GFX_getCachedText(font, text, text_color);
		bool owned = false;
		if (!text_surf) {
			text_surf = GFX_renderText(font, text, text_color);
			owned = true;
		}
		if (text_surf) {
			SDL_Rect src = {0, 0, text_surf->w > max_text_width ? max_text_width : text_surf->w, text_surf->h};
			SDL_BlitSurface(text_surf, &src, screen, &(SDL_Rect){text_x, text_y, 0, 0});
			if (owned)
				SDL_FreeSurface(text_surf);
		}
	}

	if (old_clip.w > 0 && old_clip.h > 0)
		SDL_SetClipRect(screen, &old_clip);
	else
		SDL_SetClipRect(screen, NULL);
}

// ============================================
// Badged Pill Rendering
// ============================================

ListItemBadgedPos UI_renderListItemPillBadged(
	SDL_Surface* screen, ListLayout* layout,
	TTF_Font* title_font, TTF_Font* subtitle_font, TTF_Font* badge_font,
	const char* text, const char* subtitle, char* truncated,
	int y, bool selected, int badge_width, int extra_subtitle_width) {
	ListItemBadgedPos pos;
	int item_h = SCALE1(PILL_SIZE) * 3 / 2;

	// Badge area: badge content + BUTTON_PADDING on each side
	int badge_area_w = badge_width > 0 ? badge_width + SCALE1(BUTTON_PADDING * 2) : 0;

	// Calculate title pill width (reduced max to leave room for badge area)
	int title_max_width = layout->max_width - badge_area_w;
	pos.pill_width = UI_calcListPillWidth(title_font, text, truncated, title_max_width, 0);

	// Expand pill if subtitle is wider than title
	if (subtitle && subtitle[0]) {
		int sub_w;
		GFX_measureText(subtitle_font, subtitle, &sub_w, NULL);
		sub_w += extra_subtitle_width;
		int sub_pill_w = MIN(title_max_width, sub_w + SCALE1(BUTTON_PADDING * 2));
		if (sub_pill_w > pos.pill_width)
			pos.pill_width = sub_pill_w;
	}

	if (selected) {
		int px = SCALE1(PADDING);

		if (badge_area_w > 0) {
			// Layer 1: THEME_COLOR2 outer capsule covering title + badge area
			int total_w = pos.pill_width + badge_area_w;
			UI_fillRoundedRect(screen, px, y, total_w, item_h, item_h / 3, THEME_COLOR2);
		}

		// Layer 2 (or only layer): THEME_COLOR1 inner capsule for title area
		UI_fillRoundedRect(screen, px, y, pos.pill_width, item_h, item_h / 3, THEME_COLOR1);
	}

	// Text positions: two rows vertically centered
	int text_start_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
	int title_h = TTF_FontHeight(title_font);
	int sub_h = TTF_FontHeight(subtitle_font);
	int total_text_h = title_h + sub_h;
	int top_gap = (item_h - total_text_h) / 2;

	pos.text_x = text_start_x;
	pos.text_y = y + top_gap;

	pos.subtitle_x = text_start_x;
	pos.subtitle_y = y + top_gap + title_h;

	// Badge position (centered vertically in capsule)
	pos.badge_x = SCALE1(PADDING) + pos.pill_width + SCALE1(BUTTON_PADDING);
	pos.badge_y = y + (item_h - TTF_FontHeight(badge_font)) / 2;

	// Account for right-side capsule radius reducing usable text width
	int r = item_h / 2;
	pos.text_max_width = pos.pill_width - SCALE1(BUTTON_PADDING) - r / 2;

	pos.total_width = pos.pill_width + badge_area_w;

	return pos;
}

// ============================================
// Settings Page Component
// ============================================

void UI_renderSettingsPage(SDL_Surface* screen, ListLayout* layout,
						   UISettingsItem* items, int count,
						   int selected, int* scroll,
						   const char* status_msg) {
	if (count == 0)
		return;

	int hw = screen->w;

	int total_rows = SETTINGS_ROW_COUNT;
	layout->item_h = layout->list_h / total_rows;
	layout->items_per_page = total_rows - 2;
	int y_offset = layout->item_h / 2;

	UI_adjustListScroll(selected, scroll, layout->items_per_page);

	int start = *scroll;
	int end = start + layout->items_per_page;
	if (end > count)
		end = count;

	for (int vi = start; vi < end; vi++) {
		UISettingsItem* item = &items[vi];
		int sel = (vi == selected);
		int item_y = layout->list_y + y_offset + (vi - start) * layout->item_h;

		// Custom draw override
		if (item->custom_draw) {
			item->custom_draw(screen, item->custom_draw_ctx, SCALE1(PADDING), item_y,
							  hw - SCALE1(PADDING * 2), layout->item_h, sel);
			continue;
		}

		// Format display value (add arrows for cycleable items when selected)
		char display_val[256];
		const char* display_ptr = NULL;
		if (item->value) {
			if (sel && item->cycleable)
				snprintf(display_val, sizeof(display_val), "< %s >", item->value);
			else
				snprintf(display_val, sizeof(display_val), "%s", item->value);
			display_ptr = display_val;
		}

		UI_renderSettingsRow(screen, layout, item->label, display_ptr,
							 item_y, sel, item->swatch);
	}

	// Scroll indicators
	UI_renderScrollIndicators(screen, *scroll, layout->items_per_page, count);

	// Status message centered below items (e.g. "Scanning for networks...")
	if (status_msg && status_msg[0] && count < layout->items_per_page) {
		int msg_row_y = layout->list_y + y_offset + count * layout->item_h;
		int empty_h = (layout->items_per_page - count) * layout->item_h;
		int msg_y = msg_row_y + (empty_h - TTF_FontHeight(font.small)) / 2;
		SDL_Surface* msg_surf = GFX_renderText(font.small, status_msg, COLOR_GRAY);
		if (msg_surf) {
			int msg_x = (hw - msg_surf->w) / 2;
			SDL_BlitSurface(msg_surf, NULL, screen, &(SDL_Rect){msg_x, msg_y, 0, 0});
			SDL_FreeSurface(msg_surf);
		}
	}

	// Description text in the last row (row 9)
	if (selected >= 0 && selected < count &&
		items[selected].desc && items[selected].desc[0]) {
		int desc_row_y = layout->list_y + y_offset + layout->items_per_page * layout->item_h;
		int desc_y = desc_row_y + (layout->item_h - TTF_FontHeight(font.tiny)) / 2;
		int desc_max_w = hw - SCALE1(PADDING * 2);

		char truncated_desc[256];
		GFX_truncateText(font.tiny, items[selected].desc, truncated_desc, desc_max_w, 0);

		SDL_Surface* desc_surf = GFX_renderText(font.tiny, truncated_desc, COLOR_GRAY);
		if (desc_surf) {
			int desc_x = (hw - desc_surf->w) / 2;
			SDL_BlitSurface(desc_surf, NULL, screen, &(SDL_Rect){desc_x, desc_y, 0, 0});
			SDL_FreeSurface(desc_surf);
		}
	}
}

// ============================================
// Settings Row Rendering
// ============================================

#define SETTINGS_ROW_PADDING 8

int UI_renderSettingsRow(SDL_Surface* screen, ListLayout* layout,
						 const char* label, const char* value,
						 int y, bool selected, int swatch_color) {
	int hw = screen->w;
	TTF_Font* f = font.small;

	// Measure label
	int text_w, text_h;
	GFX_measureText(f, label, &text_w, &text_h);
	int label_pill_width = text_w + SCALE1(SETTINGS_ROW_PADDING * 2);

	int pill_h = layout->item_h;
	int text_x = SCALE1(PADDING) + SCALE1(SETTINGS_ROW_PADDING);
	int text_y = y + (pill_h - TTF_FontHeight(f)) / 2;

	if (selected) {
		SDL_Color selected_text_color = UI_getListTextColor(1);

		if (value) {
			// 2-layer: full-width THEME_COLOR2 + label-width THEME_COLOR1
			int row_width = hw - SCALE1(PADDING * 2);
			SDL_Rect row_rect = {SCALE1(PADDING), y, row_width, pill_h};
			GFX_blitRectColor(ASSET_BUTTON, screen, &row_rect, THEME_COLOR2);

			SDL_Rect label_pill_rect = {SCALE1(PADDING), y, label_pill_width, pill_h};
			GFX_blitRectColor(ASSET_BUTTON, screen, &label_pill_rect, THEME_COLOR1);

			// Label text
			SDL_Surface* label_surf = GFX_renderText(f, label, selected_text_color);
			if (label_surf) {
				SDL_BlitSurface(label_surf, NULL, screen, &(SDL_Rect){text_x, text_y, 0, 0});
				SDL_FreeSurface(label_surf);
			}

			// Value with arrows, right-aligned, white text
			int value_x = hw - SCALE1(PADDING) - SCALE1(SETTINGS_ROW_PADDING);
			int val_text_y = y + (pill_h - TTF_FontHeight(font.tiny)) / 2;

			// Color swatch
			if (swatch_color >= 0) {
				int swatch_size = SCALE1(FONT_TINY);
				int swatch_y = y + (pill_h - swatch_size) / 2;
				SDL_Rect border = {value_x - swatch_size, swatch_y, swatch_size, swatch_size};
				SDL_FillRect(screen, &border, RGB_WHITE);
				SDL_Rect inner = {border.x + 1, border.y + 1, border.w - 2, border.h - 2};
				uint32_t col = (uint32_t)swatch_color;
				uint32_t mapped = SDL_MapRGB(screen->format,
											 (col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF);
				SDL_FillRect(screen, &inner, mapped);
				value_x -= swatch_size + SCALE1(4);
			}

			SDL_Surface* val_surf = GFX_renderText(font.tiny, value, COLOR_WHITE);
			if (val_surf) {
				value_x -= val_surf->w;
				SDL_BlitSurface(val_surf, NULL, screen, &(SDL_Rect){value_x, val_text_y, 0, 0});
				SDL_FreeSurface(val_surf);
			}
			return value_x;
		} else {
			// Single label rect only
			SDL_Rect label_pill_rect = {SCALE1(PADDING), y, label_pill_width, pill_h};
			GFX_blitRectColor(ASSET_BUTTON, screen, &label_pill_rect, THEME_COLOR1);

			SDL_Surface* label_surf = GFX_renderText(f, label, selected_text_color);
			if (label_surf) {
				SDL_BlitSurface(label_surf, NULL, screen, &(SDL_Rect){text_x, text_y, 0, 0});
				SDL_FreeSurface(label_surf);
			}
			return text_x;
		}
	} else {
		// Unselected: no background
		SDL_Color text_color = UI_getListTextColor(0);

		SDL_Surface* label_surf = GFX_renderText(f, label, text_color);
		if (label_surf) {
			SDL_BlitSurface(label_surf, NULL, screen, &(SDL_Rect){text_x, text_y, 0, 0});
			SDL_FreeSurface(label_surf);
		}

		if (value) {
			int value_x = hw - SCALE1(PADDING) - SCALE1(SETTINGS_ROW_PADDING);
			int val_text_y = y + (pill_h - TTF_FontHeight(font.tiny)) / 2;

			// Color swatch
			if (swatch_color >= 0) {
				int swatch_size = SCALE1(FONT_TINY);
				int swatch_y = y + (pill_h - swatch_size) / 2;
				SDL_Rect border = {value_x - swatch_size, swatch_y, swatch_size, swatch_size};
				SDL_FillRect(screen, &border, RGB_WHITE);
				SDL_Rect inner = {border.x + 1, border.y + 1, border.w - 2, border.h - 2};
				uint32_t col = (uint32_t)swatch_color;
				uint32_t mapped = SDL_MapRGB(screen->format,
											 (col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF);
				SDL_FillRect(screen, &inner, mapped);
				value_x -= swatch_size + SCALE1(4);
			}

			SDL_Surface* val_surf = GFX_renderText(font.tiny, value, text_color);
			if (val_surf) {
				value_x -= val_surf->w;
				SDL_BlitSurface(val_surf, NULL, screen, &(SDL_Rect){value_x, val_text_y, 0, 0});
				SDL_FreeSurface(val_surf);
			}
			return value_x;
		}
		return text_x;
	}
}

// ============================================
// GPU Scroll Without Background
// ============================================

void ScrollText_renderGPU_NoBg(ScrollTextState* state, TTF_Font* font,
							   SDL_Color color, int x, int y) {
	if (!state->text[0] || !state->needs_scroll || !state->cached_scroll_surface) {
		PLAT_clearLayers(LAYER_SCROLLTEXT);
		return;
	}

	state->last_x = x;
	state->last_y = y;
	state->last_font = font;
	state->last_color = color;

	int padding = SCALE1(SCROLL_GAP);
	int height = state->cached_scroll_surface->h;

	SDL_Surface* clipped = SDL_CreateRGBSurfaceWithFormat(0,
														  state->max_width, height, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!clipped)
		return;

	SDL_FillRect(clipped, NULL, 0);
	SDL_SetSurfaceBlendMode(state->cached_scroll_surface, SDL_BLENDMODE_NONE);
	SDL_Rect src = {state->scroll_offset, 0, state->max_width, height};
	SDL_BlitSurface(state->cached_scroll_surface, &src, clipped, NULL);

	PLAT_clearLayers(LAYER_SCROLLTEXT);
	PLAT_drawOnLayer(clipped, x, y, state->max_width, height, 1.0f, false, LAYER_SCROLLTEXT);
	SDL_FreeSurface(clipped);

	if (state->rtl) {
		state->scroll_offset -= 1; // window moves left -> text scrolls right
		if (state->scroll_offset < 0)
			state->scroll_offset = state->text_width + padding - 1;
	} else {
		state->scroll_offset += 1;
		if (state->scroll_offset >= state->text_width + padding)
			state->scroll_offset = 0;
	}

	PLAT_GPU_Flip();
}

// ============================================
// Scroll Helpers
// ============================================

void UI_adjustListScroll(int selected, int* scroll, int items_per_page) {
	if (selected < *scroll) {
		*scroll = selected;
	}
	if (selected >= *scroll + items_per_page) {
		*scroll = selected - items_per_page + 1;
	}
}

void UI_renderScrollIndicators(SDL_Surface* screen, int scroll, int items_per_page, int total_count) {
	if (total_count <= items_per_page)
		return;

	int hw = screen->w;
	int hh = screen->h;
	int ox = (hw - SCALE1(24)) / 2;

	if (scroll > 0) {
		GFX_blitAsset(ASSET_SCROLL_UP, NULL, screen, &(SDL_Rect){ox, SCALE1(PADDING + PILL_SIZE - BUTTON_MARGIN)});
	}
	if (scroll + items_per_page < total_count) {
		int bottom_y = hh - SCALE1(PADDING + BUTTON_SIZE + BUTTON_MARGIN) - SCALE1(8);
		GFX_blitAsset(ASSET_SCROLL_DOWN, NULL, screen, &(SDL_Rect){ox, bottom_y});
	}
}

// ============================================
// Pill Animation (non-threaded, for main-loop driven apps)
// ============================================

// Duration of the selection pill's glide to a new row, in milliseconds. The
// glide is time-based (elapsed vs SDL_GetTicks) so it lasts the same wall-clock
// time no matter how fast the caller redraws — lower = snappier. Kept at 150ms:
// the number of interpolation frames is duration / frame time, and a nextui
// list frame measures ~30ms on device (vsync-locked, ~2 refresh periods of
// CPU+GPU work — see 2026-08-10 frame profiling). 80ms yielded ~3 frames and
// read as a snap; 150ms gives ~5 evenly-paced frames, which with the
// smoothstep easing below reads as a glide while staying responsive.
#define PILL_ANIM_MS 150


void UI_pillAnimSetTarget(PillAnimState* state, int target_y, int target_w, bool animate) {
	if (target_y == state->current_y && target_w == state->current_w && !state->active)
		return;

	if (!animate) {
		state->current_y = target_y;
		state->target_y = target_y;
		state->current_w = target_w;
		state->target_w = target_w;
		state->active = false;
		return;
	}

	state->start_y = state->current_y;
	state->target_y = target_y;
	state->start_w = state->current_w;
	state->target_w = target_w;
	state->start_time = SDL_GetTicks();
	state->active = true;
}

int UI_pillAnimTick(PillAnimState* state) {
	if (!state->active)
		return state->current_y;

	uint32_t elapsed = SDL_GetTicks() - state->start_time;
	if (elapsed >= PILL_ANIM_MS) {
		state->current_y = state->target_y;
		state->current_w = state->target_w;
		state->active = false;
		return state->current_y;
	}

	float t = (float)elapsed / PILL_ANIM_MS;
	// smoothstep (ease-in-out): peak velocity mid-glide so the travel itself is
	// visible. The previous ease-out quad front-loaded ~75% of the distance into
	// the first frames, which read as a snap rather than a glide.
	t = t * t * (3.0f - 2.0f * t);
	state->current_y = state->start_y + (int)((state->target_y - state->start_y) * t);
	state->current_w = state->start_w + (int)((state->target_w - state->start_w) * t);
	return state->current_y;
}

bool UI_pillAnimIsActive(PillAnimState* state) {
	return state->active;
}

// ============================================
// List Selection Glide (shared wiring around the pill animation)
// ============================================

ListGlideFrame UI_listGlideDrawAtY(ListGlide* g, SDL_Surface* screen,
								   const void* list_id, int target_y,
								   int band_y, int band_h,
								   int item_h, int pill_w, bool allow_anim) {
	ListGlideFrame f = {target_y, false};
	if (band_h <= 0)
		return f;

	// List content changed (or first ever draw): snap, never glide across
	// contexts (folder change, page push/pop, tab switch).
	bool list_changed = (list_id != g->list_id) || g->list_id == NULL;
	g->list_id = list_id;

	if (target_y != g->prev_target_y || list_changed ||
		(!allow_anim && g->anim.active)) {
		bool animate = allow_anim && !list_changed;
		// An idle jump of more than one row (wrap last<->first, page jump)
		// enters through the near edge in the direction of travel instead
		// of sweeping the whole band: seed the glide one row past the
		// target, on the side the pill is coming from. Mid-glide retargets
		// (held repeat) keep gliding from the current position instead.
		if (animate && !g->anim.active &&
			abs(target_y - g->anim.current_y) > item_h)
			g->anim.current_y = target_y +
								(g->anim.current_y > target_y ? -item_h : item_h);
		UI_pillAnimSetTarget(&g->anim, target_y, pill_w, animate);
		g->prev_target_y = target_y;
	}

	f.pill_y = UI_pillAnimTick(&g->anim);
	f.animating = UI_pillAnimIsActive(&g->anim);
	if (!f.animating) {
		// Width changes outside a glide (label refresh, truncation change)
		// snap directly — only selection moves animate.
		g->anim.current_w = pill_w;
		g->anim.target_w = pill_w;
	}

	if (pill_w > 0) {
		// Clip to the band so an edge-entry pill is revealed through the
		// first/last row instead of sliding over the menu/hint bars.
		SDL_Rect prev_clip;
		SDL_GetClipRect(screen, &prev_clip);
		SDL_SetClipRect(screen, &(SDL_Rect){0, band_y, screen->w, band_h});
		UI_drawListItemBg(screen,
						  &(SDL_Rect){SCALE1(PADDING), f.pill_y,
									  g->anim.current_w, item_h},
						  true);
		SDL_SetClipRect(screen, &prev_clip);
	}
	return f;
}

bool UI_listGlideRowSelected(const ListGlideFrame* f, int row_y, int item_h) {
	return abs(f->pill_y - row_y) * 2 < item_h;
}

bool UI_listGlideActive(ListGlide* g) {
	if (!g->anim.active)
		return false;
	// A glide can be abandoned mid-flight (screen exit, list emptied): only
	// a draw's tick clears `active`, so hooked dirty loops would spin
	// forever on screens that never draw this list. Once the glide's time
	// is up, finalize it here (tick clamps to target and clears active) and
	// report active one last time so the host draws exactly one settled
	// frame — on which f.animating is false, letting the marquee start.
	if (SDL_GetTicks() - g->anim.start_time >= PILL_ANIM_MS)
		UI_pillAnimTick(&g->anim);
	return true;
}

// ============================================
// Rich Pill Rendering
// ============================================

ListItemRichPos UI_renderListItemPillRich(SDL_Surface* screen, ListLayout* layout,
										  const char* title, const char* subtitle,
										  char* truncated,
										  int y, bool selected, bool has_image,
										  int extra_subtitle_width) {
	ListItemRichPos pos;

	int item_h = SCALE1(PILL_SIZE) * 3 / 2;
	int img_padding = SCALE1(4);

	int image_area_w;
	if (has_image) {
		pos.image_size = item_h - img_padding * 2;
		image_area_w = img_padding + pos.image_size + SCALE1(BUTTON_PADDING);
		pos.image_x = SCALE1(PADDING) + img_padding;
		pos.image_y = y + img_padding;
	} else {
		pos.image_size = 0;
		image_area_w = SCALE1(BUTTON_PADDING);
		pos.image_x = 0;
		pos.image_y = 0;
	}

	pos.pill_width = UI_calcListPillWidth(font.medium, title, truncated, layout->max_width, image_area_w);
	if (subtitle && subtitle[0]) {
		int sub_w;
		GFX_measureText(font.small, subtitle, &sub_w, NULL);
		int sub_pill_w = MIN(layout->max_width, image_area_w + sub_w + extra_subtitle_width + SCALE1(BUTTON_PADDING * 2));
		if (sub_pill_w > pos.pill_width)
			pos.pill_width = sub_pill_w;
	}

	if (selected) {
		UI_fillRoundedRect(screen, SCALE1(PADDING), y, pos.pill_width, item_h,
						   item_h / 3, THEME_COLOR1);
	}

	int text_start_x = SCALE1(PADDING) + image_area_w;
	int medium_h = TTF_FontHeight(font.medium);
	int small_h = TTF_FontHeight(font.small);
	int total_text_h = medium_h + small_h;
	int top_gap = (item_h - total_text_h) / 2;

	pos.title_x = text_start_x;
	pos.title_y = y + top_gap;

	pos.subtitle_x = text_start_x;
	pos.subtitle_y = y + top_gap + medium_h;

	pos.text_max_width = pos.pill_width - image_area_w - SCALE1(BUTTON_PADDING);

	return pos;
}

// ============================================
// Menu Item Pill Rendering
// ============================================

MenuItemPos UI_renderMenuItemPill(SDL_Surface* screen, ListLayout* layout,
								  const char* text, char* truncated,
								  int index, bool selected, int prefix_width) {
	MenuItemPos pos;

	int item_h = SCALE1(PILL_SIZE);
	pos.item_y = layout->list_y + index * item_h;

	pos.pill_width = UI_calcListPillWidth(font.large, text, truncated, layout->max_width - prefix_width, prefix_width);

	SDL_Rect pill_rect = {SCALE1(PADDING), pos.item_y, pos.pill_width, SCALE1(PILL_SIZE)};
	UI_drawListItemBg(screen, &pill_rect, selected);

	pos.text_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
	pos.text_y = pos.item_y + (SCALE1(PILL_SIZE) - TTF_FontHeight(font.large)) / 2;

	return pos;
}

// ============================================
// Rounded Rectangle Background
// ============================================

void UI_renderRoundedRectBg(SDL_Surface* screen, int x, int y, int w, int h, uint32_t color) {
	UI_fillRoundedRect(screen, x, y, w, h, SCALE1(7), color);
}
