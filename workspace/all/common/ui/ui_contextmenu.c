#include "ui_contextmenu.h"
#include "ui_list.h"
#include "ui_draw.h"
#include "api.h"
#include "defines.h"
#include <string.h>

static ContextMenuItem cm_items[CONTEXTMENU_MAX_ITEMS];
static int cm_count;
static int cm_selected;
static bool cm_open;
static int cm_close_guard; // counts down frames after close to prevent reopen

void ContextMenu_open(ContextMenuItem* items, int count) {
	if (count > CONTEXTMENU_MAX_ITEMS)
		count = CONTEXTMENU_MAX_ITEMS;
	memcpy(cm_items, items, count * sizeof(ContextMenuItem));
	cm_count = count;
	cm_selected = 0;
	cm_open = true;
}

ContextMenuResult ContextMenu_handleInput(void) {
	ContextMenuResult result = {CONTEXTMENU_NONE, -1};

	// Tick close guard (once per frame via this call)
	if (cm_close_guard > 0) {
		cm_close_guard--;
		return result;
	}

	if (!cm_open)
		return result;

	// MENU closes on the RELEASE edge: the release is what re-triggers
	// PAD_tappedMenu in the game list, so it must land inside the close-guard
	// window — closing on the press meant the release arrived frames after
	// the guard expired and instantly reopened the menu (broken toggle).
	if (PAD_justPressed(BTN_B) || PAD_justReleased(BTN_MENU)) {
		result.action = CONTEXTMENU_CANCEL;
		cm_open = false;
		cm_close_guard = 2;
		GFX_clearLayers(LAYER_OVERLAY);
		return result;
	}

	if (cm_count == 0)
		return result;

	if (PAD_justPressed(BTN_A)) {
		result.action = CONTEXTMENU_SELECTED;
		result.id = cm_items[cm_selected].id;
		cm_open = false;
		cm_close_guard = 2;
		GFX_clearLayers(LAYER_OVERLAY);
		return result;
	}

	PAD_navigateMenu(&cm_selected, cm_count);

	return result;
}

void ContextMenu_render(SDL_Surface* screen) {
	if (!cm_open || cm_count == 0)
		return;

	int sw = screen->w;
	int sh = screen->h;

	// Render onto an ARGB surface, then blit to LAYER_OVERLAY (on top of everything)
	static SDL_Surface* surf = NULL;
	if (!surf || surf->w != sw || surf->h != sh) {
		if (surf)
			SDL_FreeSurface(surf);
		surf = SDL_CreateRGBSurface(SDL_SWSURFACE, sw, sh, 32,
									0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
		if (!surf)
			return;
	}

	// Semi-transparent backdrop
	SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 0, 0, 0, 178));

	// Layout calculations
	int pad = SCALE1(PADDING);
	int item_h = SCALE1(PILL_SIZE) * 3 / 4;
	int panel_w = sw * 3 / 4;
	int panel_h = item_h * cm_count + pad * 2;
	int panel_x = (sw - panel_w) / 2;
	int panel_y = (sh - panel_h) / 2;

	// Panel background (dark rounded rect)
	uint32_t panel_color = SDL_MapRGBA(surf->format, 30, 30, 30, 255);
	UI_fillRoundedRect(surf, panel_x, panel_y, panel_w, panel_h, SCALE1(8), panel_color);

	// Items
	int items_y = panel_y + pad;
	for (int i = 0; i < cm_count; i++) {
		int y = items_y + i * item_h;
		bool selected = (i == cm_selected);

		// Selection highlight (themed rounded pill)
		if (selected) {
			// Convert THEME_COLOR1 (RGB565/RGB888 mapped to screen format) to ARGB for our surface
			uint8_t cr, cg, cb;
			SDL_GetRGB(THEME_COLOR1, screen->format, &cr, &cg, &cb);
			uint32_t pill_color = SDL_MapRGBA(surf->format, cr, cg, cb, 255);
			UI_fillRoundedRect(surf, panel_x + pad, y, panel_w - pad * 2, item_h,
							   item_h / 3, pill_color);
		}

		// Item text (themed colors matching list items)
		SDL_Color text_color = UI_getListTextColor(selected);
		SDL_Surface* text_surf = TTF_RenderUTF8_Blended(font.small, cm_items[i].label, text_color);
		if (text_surf) {
			SDL_SetSurfaceBlendMode(text_surf, SDL_BLENDMODE_BLEND);
			int text_x = panel_x + pad + SCALE1(BUTTON_PADDING);
			int text_y = y + (item_h - text_surf->h) / 2;
			SDL_BlitSurface(text_surf, NULL, surf, &(SDL_Rect){text_x, text_y, 0, 0});
			SDL_FreeSurface(text_surf);
		}
	}

	// Clear and draw on the topmost GPU layer
	GFX_clearLayers(LAYER_OVERLAY);
	GFX_drawOnLayer(surf, 0, 0, sw, sh, 1.0f, false, LAYER_OVERLAY);
}


bool ContextMenu_isOpen(void) {
	return cm_open || cm_close_guard > 0;
}
