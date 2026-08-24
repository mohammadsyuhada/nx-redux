// Unified list widget: render + state + input for the OS's pill lists,
// composed from the ui_list.c primitives (glide core, marquee, pill
// helpers). Spec: docs/superpowers/specs/2026-08-10-unified-listview-design.md
#include <stdlib.h>
#include <string.h>
#include "ui_listview.h"
#include "api.h"
#include "defines.h"
#include "ui_buttonhintbar.h"
#include "ui_menubar.h"
#include "ui_emptystate.h"

#define LISTVIEW_ICON_SIZE 24 // logical px, SCALE1'd at draw
#define LISTVIEW_ICON_SPACING 6
#define LISTVIEW_MAX_VISIBLE 32 // list_h / (item_h*3/4) tops out well below this

static void get_row_safe(ListView* v, int i, bool selected, ListViewRow* out) {
	memset(out, 0, sizeof(*out));
	// Provider may not be wired yet: full-mode callers UI_listViewReset on
	// module entry, but get_row/ctx are only assigned by the first render -
	// input runs first that frame.
	if (v->get_row)
		v->get_row(v->ctx, i, selected, out);
	if (!out->label)
		out->label = ""; // normalizes both the provider and no-provider paths
}

static bool row_is_header(ListView* v, int i) {
	ListViewRow row;
	get_row_safe(v, i, false, &row);
	return row.is_header;
}

// Header rows: a quarter-row breathing gap above (separating the header
// from the previous group's last row - dropped when the header is the
// list's first row, where there is nothing to separate from), label in a
// half-row band, and a quarter-row gap below (generalizes the extras
// catalog layout).
static int header_top_gap(int i, int item_h) {
	return i > 0 ? item_h / 4 : 0;
}
static int row_height(ListView* v, int i, int item_h) {
	return row_is_header(v, i)
			   ? header_top_gap(i, item_h) + item_h / 2 + item_h / 4
			   : item_h;
}

static TTF_Font* row_font(ListView* v) {
	return v->font ? v->font : font.large;
}

// Step from `from` by dir (+1/-1), skipping headers, wrapping unless
// no_wrap. Returns `from` when no other selectable row exists (guards the
// all-headers degenerate list against infinite skip loops).
static int step_selectable(ListView* v, int from, int dir) {
	int i = from;
	for (int n = 0; n < v->count; n++) {
		i += dir;
		if (i < 0) {
			if (v->no_wrap)
				return from;
			i = v->count - 1;
		} else if (i >= v->count) {
			if (v->no_wrap)
				return from;
			i = 0;
		}
		if (!row_is_header(v, i))
			return i;
	}
	return from;
}

// Nearest selectable row to `target` (clamped), preferring direction `dir`,
// falling back to the other. For LEFT/RIGHT page jumps.
static int nearest_selectable(ListView* v, int target, int dir) {
	if (target < 0)
		target = 0;
	if (target >= v->count)
		target = v->count - 1;
	for (int i = target; i >= 0 && i < v->count; i += dir)
		if (!row_is_header(v, i))
			return i;
	for (int i = target; i >= 0 && i < v->count; i -= dir)
		if (!row_is_header(v, i))
			return i;
	return target;
}

// True when every row strictly between a and b is a header: an "adjacent"
// selection step for glide purposes, even though the index moved by 2+.
static bool adjacent_selectable(ListView* v, int a, int b) {
	if (a < 0 || b < 0 || a == b)
		return false;
	int lo = a < b ? a : b;
	int hi = a > b ? a : b;
	for (int i = lo + 1; i < hi; i++)
		if (!row_is_header(v, i))
			return false;
	return true;
}

ListViewAction UI_listViewHandleInput(ListView* v) {
	ListViewAction a = {LISTVIEW_NONE, v->count > 0 ? v->selected : -1, 0};

	if (v->count > 0) {
		int page = v->last_visible > 0 ? v->last_visible : 1;
		int prev = v->selected;
		if (PAD_justRepeated(BTN_UP)) {
			v->selected = step_selectable(v, v->selected, -1);
		} else if (PAD_justRepeated(BTN_DOWN)) {
			v->selected = step_selectable(v, v->selected, +1);
		} else if (!v->no_lr_paging && PAD_justRepeated(BTN_LEFT)) {
			v->selected = nearest_selectable(v, v->selected - page, -1);
		} else if (!v->no_lr_paging && PAD_justRepeated(BTN_RIGHT)) {
			v->selected = nearest_selectable(v, v->selected + page, +1);
		}
		if (v->selected != prev) {
			v->input_pending = true;
			a.index = v->selected;
		}
	}

	if (PAD_justPressed(BTN_A) && v->count > 0 &&
		!row_is_header(v, v->selected)) {
		a.type = LISTVIEW_ACTIVATED;
		return a;
	}
	if (PAD_justPressed(BTN_A) && v->count == 0) {
		// Empty list: report A as a button so empty-state actions (e.g. an
		// advertised A/NEW) can fire; index is already -1.
		a.type = LISTVIEW_BUTTON;
		a.btn = BTN_A;
		return a;
	}
	if (PAD_justPressed(BTN_B)) {
		a.type = LISTVIEW_BACK;
		return a;
	}
	if (PAD_tappedMenu(SDL_GetTicks())) {
		a.type = LISTVIEW_MENU;
		return a;
	}
	if (PAD_justPressed(BTN_X)) {
		a.type = LISTVIEW_BUTTON;
		a.btn = BTN_X;
		return a;
	}
	if (PAD_justPressed(BTN_Y)) {
		a.type = LISTVIEW_BUTTON;
		a.btn = BTN_Y;
		return a;
	}
	return a;
}

// First letter of row i's label, uppercased ('a'-'z' -> 'A'-'Z'); leading
// spaces skipped. Used only by the initial-jump navigation below.
static char row_initial(ListView* v, int i) {
	ListViewRow row;
	get_row_safe(v, i, false, &row);
	const char* s = row.label;
	while (*s == ' ')
		s++;
	char c = *s;
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	return c;
}

bool UI_listViewJumpInitial(ListView* v, int dir) {
	if (!v || v->count <= 1)
		return false;
	int n = v->count;
	int sel = v->selected;
	if (sel < 0)
		sel = 0;
	if (sel >= n)
		sel = n - 1;
	char cur = row_initial(v, sel);
	int target;
	if (dir > 0) {
		int i = sel + 1;
		while (i < n && row_initial(v, i) == cur)
			i++;
		if (i >= n)
			return false; // already in the last letter group
		target = i;		  // first row of the next group
	} else {
		int i = sel - 1;
		while (i >= 0 && row_initial(v, i) == cur)
			i--;
		if (i < 0)
			return false; // already in the first letter group
		// i is the last row of the previous group; walk back to its start.
		char prev = row_initial(v, i);
		while (i > 0 && row_initial(v, i - 1) == prev)
			i--;
		target = i;
	}
	if (row_is_header(v, target))
		target = nearest_selectable(v, target, dir > 0 ? +1 : -1);
	if (target < 0 || target >= n || target == v->selected)
		return false;
	v->selected = target;
	v->input_pending = true;
	return true;
}

void UI_listViewReset(ListView* v, int count, const void* list_id) {
	v->count = count;
	v->list_id = list_id;
	v->selected = 0;
	v->scroll = 0;
	v->drawn_id = list_id;
	v->drawn_count = count;
	v->prev_selected = -1;
	v->input_pending = true;
	GFX_clearLayers(LAYER_SCROLLTEXT);
	ScrollText_clear(&v->marquee);
	// glide: the list_id change (or same-id relocation's own travel) is
	// handled by UI_listGlideDrawAtY on the next render.
}

bool UI_listViewBusy(ListView* v) {
	return v->input_pending || UI_listGlideActive(&v->glide) ||
		   ScrollText_needsRender(&v->marquee);
}

bool UI_listViewMarqueeBusy(ListView* v) {
	return ScrollText_isScrolling(&v->marquee) ||
		   ScrollText_needsRender(&v->marquee);
}

void UI_listViewTickIdle(ListView* v) {
	ScrollText_activateAfterDelay(&v->marquee);
	if (ScrollText_isScrolling(&v->marquee))
		ScrollText_animateOnly(&v->marquee);
}

void UI_listViewRender(ListView* v, SDL_Surface* screen) {
	// 1. Guards. Defensive twin of UI_listViewReset: clears the marquee and
	// lets the id change snap the glide, but preserves selected/scroll -
	// hosted callers (settings page stack) sync a restored cursor before
	// calling render, and full-mode callers are expected to have called
	// UI_listViewReset themselves. Selection is clamped below either way.
	if (v->list_id != v->drawn_id || v->count != v->drawn_count) {
		v->drawn_id = v->list_id;
		v->drawn_count = v->count;
		v->prev_selected = -1;
		GFX_clearLayers(LAYER_SCROLLTEXT);
		ScrollText_clear(&v->marquee);
	}
	v->input_pending = false;

	ListLayout layout = UI_calcListLayout(screen);
	if (v->list_y_override > 0) {
		layout.list_y = v->list_y_override;
		layout.list_h = screen->h - layout.list_y -
						SCALE1(PADDING + BUTTON_SIZE + BUTTON_MARGIN);
		layout.items_per_page = layout.list_h / layout.item_h;
	}
	if (v->max_width_override > 0)
		layout.max_width = v->max_width_override;

	if (v->title)
		UI_renderMenuBar(screen, v->title);

	if (v->count == 0) {
		v->last_visible = 0;
		if (v->empty_title) {
			if (v->empty_btn_pairs)
				UI_renderEmptyStateButtons(screen, v->empty_title,
										   v->empty_subtitle, v->empty_btn_pairs);
			else
				UI_renderEmptyState(screen, v->empty_title, v->empty_subtitle,
									v->empty_y_label);
		}
		if (v->hint_pairs)
			UI_renderButtonHintBar(screen, v->hint_pairs);
		return;
	}

	// Clamp selection into [0, count) and off headers.
	if (v->selected < 0)
		v->selected = 0;
	if (v->selected >= v->count)
		v->selected = v->count - 1;
	if (row_is_header(v, v->selected))
		v->selected = nearest_selectable(v, v->selected, +1);
	bool sel_is_header = row_is_header(v, v->selected); // all-header list

	// 2. Pixel-based scroll windowing: walk heights from scroll until the
	// band is filled; advance scroll until selected is fully visible.
	// Uniform rows degenerate to UI_adjustListScroll behavior.
	if (v->scroll < 0)
		v->scroll = 0;
	if (v->scroll >= v->count)
		v->scroll = v->count - 1;
	if (v->selected < v->scroll) {
		// scrolling up: bring the section header along when it is the row
		// directly above, so the group reads as one unit
		v->scroll = (v->selected > 0 && row_is_header(v, v->selected - 1))
						? v->selected - 1
						: v->selected;
	}

	int first = v->scroll;
	int visible = 0;
	for (;;) {
		int y = 0;
		visible = 0;
		bool sel_visible = false;
		for (int i = first; i < v->count && visible < LISTVIEW_MAX_VISIBLE; i++) {
			int h = row_height(v, i, layout.item_h);
			if (y + h > layout.list_h)
				break;
			if (i == v->selected)
				sel_visible = true;
			y += h;
			visible++;
		}
		if (sel_visible || first >= v->selected)
			break;
		first++;
	}
	v->scroll = first;
	v->last_visible = visible;

	// 3. Glide pre-pass: per-row y in the same walk, selected row's pill
	// width (label truncation + badge reservation via the prefix path).
	int y_at[LISTVIEW_MAX_VISIBLE];
	int yy = layout.list_y;
	int sel_y = layout.list_y;
	int band_y = -1;
	int band_end = layout.list_y;
	for (int n = 0; n < visible; n++) {
		int i = first + n;
		bool hdr = row_is_header(v, i);
		y_at[n] = yy;
		if (!hdr && band_y < 0)
			band_y = yy; // pill band starts at the first entry row
		if (i == v->selected)
			sel_y = yy;
		yy += row_height(v, i, layout.item_h);
		if (!hdr)
			band_end = yy; // ...and ends after the last entry row
	}
	if (band_y < 0)
		band_y = layout.list_y;
	int band_h = band_end - band_y;

	char sel_trunc[256];
	ListViewRow sel_row;
	int sel_pill_w = 0;
	if (!sel_is_header) {
		get_row_safe(v, v->selected, true, &sel_row);
		int sel_prefix = 0;
		if (sel_row.icon)
			sel_prefix += SCALE1(LISTVIEW_ICON_SIZE + LISTVIEW_ICON_SPACING);
		if (sel_row.badge && sel_row.badge[0]) {
			int btw = 0, bth = 0;
			GFX_measureText(font.tiny, sel_row.badge, &btw, &bth);
			sel_prefix += btw + SCALE1(PADDING);
		}
		sel_pill_w = UI_calcListPillWidth(row_font(v), sel_row.label, sel_trunc,
										  layout.max_width, sel_prefix);
	}

	// Adjacent-step-across-header pre-target (lifted from extras.c): an
	// adjacent selection step whose pixel travel exceeds one row (a header
	// sits between) must read as continuous travel, not the core's
	// edge-entry jump. Pre-target the anim from its true position and mark
	// the target seen; UI_listGlideDrawAtY below then just ticks.
	if (v->list_id == v->glide.list_id && !UI_listGlideActive(&v->glide) &&
		adjacent_selectable(v, v->prev_selected, v->selected) &&
		sel_y != v->glide.prev_target_y &&
		abs(sel_y - v->glide.anim.current_y) > layout.item_h) {
		UI_pillAnimSetTarget(&v->glide.anim, sel_y, sel_pill_w, true);
		v->glide.prev_target_y = sel_y;
	}
	v->prev_selected = v->selected;

	ListGlideFrame gf = UI_listGlideDrawAtY(&v->glide, screen, v->list_id,
											sel_y, band_y, band_h,
											layout.item_h, sel_pill_w, true);

	// 4. Row loop.
	char truncated[256];
	for (int n = 0; n < visible; n++) {
		int i = first + n;
		int y = y_at[n];
		bool row_sel = !sel_is_header &&
					   UI_listGlideRowSelected(&gf, y, layout.item_h);
		ListViewRow row;
		get_row_safe(v, i, row_sel, &row);

		if (row.is_header) {
			int label_y = y + header_top_gap(i, layout.item_h);
			int h = layout.item_h / 2;
			SDL_Surface* surf =
				GFX_renderText(font.small, row.label, COLOR_GRAY);
			if (surf) {
				int x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
				SDL_BlitSurface(surf, NULL, screen,
								&(SDL_Rect){x, label_y + (h - surf->h) / 2, 0, 0});
				SDL_FreeSurface(surf);
			}
			continue;
		}

		int icon_offset = 0;
		if (row.icon)
			icon_offset = SCALE1(LISTVIEW_ICON_SIZE + LISTVIEW_ICON_SPACING);
		int prefix = icon_offset;
		int b_tw = 0, b_th = 0;
		if (row.badge && row.badge[0]) {
			GFX_measureText(font.tiny, row.badge, &b_tw, &b_th);
			prefix += b_tw + SCALE1(PADDING);
		}

		ListItemPos pos = UI_renderListItemPill(screen, &layout, row_font(v),
												row.label, truncated, y,
												false, prefix);

		if (row.icon) {
			int isz = SCALE1(LISTVIEW_ICON_SIZE);
			int icon_y = y + (layout.item_h - isz) / 2;
			SDL_Rect src = {0, 0, row.icon->w, row.icon->h};
			SDL_Rect dst = {pos.text_x, icon_y, isz, isz};
			SDL_BlitScaled(row.icon, &src, screen, &dst);
		}

		int text_x = pos.text_x + icon_offset;
		int text_w = pos.pill_width - SCALE1(BUTTON_PADDING * 2) - prefix;
		// Keep the selected row's marquee band clear of the annotation.
		if (row.annotation && row.annotation[0] && row_sel) {
			int ann_w = 0;
			GFX_measureText(font.tiny, row.annotation, &ann_w, NULL);
			int ann_x = screen->w - ann_w - SCALE1(PADDING * 2);
			if (text_x + text_w > ann_x - SCALE1(BUTTON_PADDING))
				text_w = ann_x - SCALE1(BUTTON_PADDING) - text_x;
		}
		// Marquee only on the settled selected row; one state per widget.
		// Static rows draw the ellipsis-truncated label.
		if (row_sel && !gf.animating) {
			UI_renderListItemText(screen, &v->marquee, row.label, row_font(v),
								  text_x, pos.text_y, text_w, true);
		} else {
			UI_renderListItemText(screen, NULL, truncated, row_font(v),
								  text_x, pos.text_y, text_w, row_sel);
		}

		if (row.annotation && row.annotation[0]) {
			SDL_Color ann_color = row_sel ? COLOR_GRAY : COLOR_DARK_TEXT;
			SDL_Surface* ann =
				GFX_renderText(font.tiny, row.annotation, ann_color);
			if (ann) {
				SDL_BlitSurface(
					ann, NULL, screen,
					&(SDL_Rect){screen->w - ann->w - SCALE1(PADDING * 2),
								y + (layout.item_h - ann->h) / 2, 0, 0});
				SDL_FreeSurface(ann);
			}
		}

		if (row.badge && row.badge[0]) {
			// Scraper convention preserved verbatim: in-pill, right-aligned.
			SDL_Color badge_color = row_sel ? COLOR_BLACK : COLOR_GRAY;
			int badge_x = pos.pill_width - SCALE1(PADDING) - b_tw;
			GFX_blitText(font.tiny, (char*)row.badge, 0, badge_color, screen,
						 &(SDL_Rect){badge_x, pos.text_y + SCALE1(2), b_tw, b_th});
		}
	}

	// 5. Chrome.
	UI_renderScrollIndicators(screen, first, visible, v->count);
	if (v->hint_pairs)
		UI_renderButtonHintBar(screen, v->hint_pairs);
}
