#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <msettings.h>

#include "defines.h"
#include "config.h"
#include "api.h"
#include "ui_buttonhintbar.h"
#include "ui_confirmdialog.h"
#include "ui_emptystate.h"
#include "ui_menubar.h"
#include "ui_list.h"
#include "ui_list_layout.h"
#include "ui_font.h"
#include "ui_splash.h"
#include "ui_quitrequest.h"
#include "utils.h"

#include <sqlite3.h>
#include <gametimedb.h>

struct ListLayout {
	int list_display_size_x;
	int list_display_size_y;
	int list_display_start_x;
	int list_display_start_y;
	SDL_Rect list_display_rect;

	int sub_title_x;
	int sub_title_y;

	int items_per_page;
	int num_pages;

	int up_y, down_y; // arrow strip centres (UI_listBlock)
} layout = {0};


#define BIG_PILL_SIZE 40
#define IMG_MARGIN 6
#define IMG_MAX_WIDTH BIG_PILL_SIZE - IMG_MARGIN
#define IMG_MAX_HEIGHT BIG_PILL_SIZE - IMG_MARGIN

static SDL_Surface* screen;
static SDL_Surface** romImages;

static PlayActivities* play_activities;

///////

int _renderText(const char* text, TTF_Font* font, SDL_Color color, SDL_Rect* rect, bool right_align) {
	int text_width = 0;
	SDL_Surface* textSurface = GFX_renderText(font, text, color);
	if (textSurface != NULL) {
		text_width = textSurface->w;
		if (right_align)
			SDL_BlitSurface(textSurface, NULL, screen, &(SDL_Rect){rect->w - textSurface->w, rect->y, rect->w, rect->h});
		else
			SDL_BlitSurface(textSurface, NULL, screen, rect);
		SDL_FreeSurface(textSurface);
	}
	return text_width;
}

int renderText(const char* text, TTF_Font* font, SDL_Color color, SDL_Rect* rect) {
	return _renderText(text, font, color, rect, false);
}

// Set a pixel color on the surface
void _setPixel(SDL_Surface* surface, int x, int y, Uint32 color) {
	if (x < 0 || x >= surface->w || y < 0 || y >= surface->h) {
		return; // Out of bounds check
	}

	// Lock surface if needed
	SDL_LockSurface(surface);

	Uint8* pixelPtr = (Uint8*)surface->pixels + y * surface->pitch + x * surface->format->BytesPerPixel;

	switch (surface->format->BytesPerPixel) {
	case 1:
		*pixelPtr = color;
		break;
	case 2:
		*(Uint16*)pixelPtr = color;
		break;
	case 3:
		if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
			pixelPtr[0] = (color >> 16) & 0xFF;
			pixelPtr[1] = (color >> 8) & 0xFF;
			pixelPtr[2] = color & 0xFF;
		} else {
			pixelPtr[0] = color & 0xFF;
			pixelPtr[1] = (color >> 8) & 0xFF;
			pixelPtr[2] = (color >> 16) & 0xFF;
		}
		break;
	case 4:
		*(Uint32*)pixelPtr = color;
		break;
	}

	SDL_UnlockSurface(surface);
}

// Draw a filled circle
void _drawFilledCircle(SDL_Surface* surface, int cx, int cy, int radius, Uint32 color) {
	for (int y = -radius; y <= radius; y++) {
		for (int x = -radius; x <= radius; x++) {
			if (x * x + y * y <= radius * radius) {
				_setPixel(surface, cx + x, cy + y, color);
			}
		}
	}
}

// Draw a filled rounded rectangle
void renderRoundedRectangle(SDL_Rect rect, Uint32 color, int radius) {
	// Fill the center and straight edges
	SDL_Rect fillRect = {rect.x + radius, rect.y, rect.w - 2 * radius, rect.h};
	SDL_FillRect(screen, &fillRect, color);

	fillRect.x = rect.x;
	fillRect.y = rect.y + radius;
	fillRect.w = rect.w;
	fillRect.h = rect.h - 2 * radius;
	SDL_FillRect(screen, &fillRect, color);

	// Draw the corner circles
	_drawFilledCircle(screen, rect.x + radius, rect.y + radius, radius, color);							  // Top-left
	_drawFilledCircle(screen, rect.x + rect.w - radius - 1, rect.y + radius, radius, color);			  // Top-right
	_drawFilledCircle(screen, rect.x + radius, rect.y + rect.h - radius - 1, radius, color);			  // Bottom-left
	_drawFilledCircle(screen, rect.x + rect.w - radius - 1, rect.y + rect.h - radius - 1, radius, color); // Bottom-right
}

SDL_Surface* loadRomImage(char* image_path) {
	if (!exists(image_path))
		return NULL;

	SDL_Surface* img = IMG_Load(image_path);
	if (!img)
		return NULL;

	if (img->format->format != SDL_PIXELFORMAT_RGBA32) {
		SDL_Surface* optimized = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_RGBA32, 0);
		SDL_FreeSurface(img);
		img = optimized;
	}

	SDL_PixelFormat* ft = img->format;
	SDL_Surface* dst = SDL_CreateRGBSurface(0, SCALE1(IMG_MAX_WIDTH), SCALE1(IMG_MAX_HEIGHT), ft->BitsPerPixel, ft->Rmask, ft->Gmask, ft->Bmask, ft->Amask);
	SDL_Rect imgRect = GFX_blitScaled(GFX_SCALE_FILL, img, dst);
	GFX_ApplyRoundedCorners(dst, &imgRect, SCALE1(16));
	SDL_FreeSurface(img);

	return dst;
}

void preloadRomImages() {
	// load all rom images into SDL_Surfaces
	romImages = malloc(sizeof(SDL_Surface*) * play_activities->count);
	for (int i = 0; i < play_activities->count; i++) {
		PlayActivity* entry = play_activities->play_activity[i];
		ROM* rom = entry->rom;
		romImages[i] = loadRomImage(rom->image_path);
	}
}

void freeRomImages() {
	for (int i = 0; i < play_activities->count; i++) {
		SDL_FreeSurface(romImages[i]);
	}
	free(romImages);
}

void renderList(int count, int start, int end, int selected) {
	char rom_name[255];
	char total[25];
	char average[25];
	char plays[25];

	int num_width = 0;

	const int elemHeight = SCALE1(BIG_PILL_SIZE);
	const int thumbMargin = SCALE1(IMG_MARGIN);
	const int textHeight = (elemHeight - thumbMargin) / 2;

	// Row text (LIST-LAYOUT §10.3/§10.6): the game name is the list label (UI_TEXT_LABEL, 16 logical) and the
	// stats line its secondary text (UI_TEXT_SECONDARY, 0.8 x). Line boxes are 1.2 x their size with the
	// glyphs centred, and the pair is centred in the row (as on the rich rows).
	const int label_px = UI_textRolePx(UI_TEXT_LABEL);
	const int detail_px = UI_textRolePx(UI_TEXT_SECONDARY);
	const int title_box = (int)(label_px * 1.2f + 0.5f);
	const int detail_box = (int)(detail_px * 1.2f + 0.5f);
	const int pair_top = (elemHeight - title_box - detail_box) / 2;
	// each role font is fetched, measured and dropped before the next UIFont_get
	int title_h = label_px;
	{
		TTF_Font* f = UI_textRole(UI_TEXT_LABEL, false);
		if (f)
			title_h = TTF_FontHeight(f);
	}
	const int title_y = pair_top + (title_box - title_h) / 2;
	int detail_h = detail_px;
	{
		TTF_Font* f = UI_textRole(UI_TEXT_SECONDARY, false);
		if (f)
			detail_h = TTF_FontHeight(f);
	}
	const int detail_y = pair_top + title_box + (detail_box - detail_h) / 2;

	int selected_row = selected - start;
	for (int index = start, row = 0; index < end; index++, row++) {
		bool isSelected = selected_row == row;

		PlayActivity* entry = play_activities->play_activity[index];
		ROM* rom = entry->rom;

		renderRoundedRectangle((SDL_Rect){
								   layout.list_display_start_x,
								   layout.list_display_start_y + row * elemHeight,
								   layout.list_display_size_x,
								   elemHeight},
							   isSelected ? RGB_WHITE : RGB_BLACK, SCALE1(BIG_PILL_SIZE / 2));

		SDL_Surface* romImage = romImages[index];
		if (romImage) {
			SDL_Rect rectRomImage = {
				layout.list_display_start_x + num_width + thumbMargin / 2 + (SCALE1(IMG_MAX_WIDTH) - romImage->w) / 2,
				layout.list_display_start_y + elemHeight * row + thumbMargin / 2,
				SCALE1(IMG_MAX_WIDTH),
				SCALE1(IMG_MAX_HEIGHT)};
			SDL_BlitSurface(romImage, NULL, screen, &rectRomImage);
		} else {
			SDL_Rect rectRomImage = {
				layout.list_display_start_x + num_width + thumbMargin / 2,
				layout.list_display_start_y + elemHeight * row + thumbMargin / 2,
				SCALE1(IMG_MAX_WIDTH),
				SCALE1(IMG_MAX_HEIGHT)};

			renderRoundedRectangle(rectRomImage, RGB_DARK_GRAY, SCALE1(16));

			// TODO: no getter exposed for this right now
			//SDL_Rect rect = asset_rects[ASSET_GAMEPAD];
			SDL_Rect rect = (SDL_Rect){SCALE4(92, 51, 18, 10)};
			int x = rectRomImage.x;
			int y = rectRomImage.y;
			x += (SCALE1(IMG_MAX_WIDTH) - rect.w) / 2;
			y += (SCALE1(IMG_MAX_HEIGHT) - rect.h) / 2;

			GFX_blitAssetColor(ASSET_GAMEPAD, NULL, screen, &(SDL_Rect){x, y}, THEME_COLOR1);
		}

		cleanName(rom_name, rom->name);
		SDL_Color textColor = COLOR_WHITE;
		if (isSelected) {
			//textColor = colorFromUint(THEME_COLOR1);
			textColor = COLOR_BLACK;
		}
		int row_y = layout.list_display_start_y + elemHeight * row;
		int text_x = layout.list_display_start_x + num_width + thumbMargin + SCALE1(IMG_MAX_WIDTH);
		// the name ends half a row before the capsule's right end, with an ellipsis when it is longer
		{
			TTF_Font* nameFont = UI_textRole(UI_TEXT_LABEL, false);
			if (!nameFont)
				nameFont = font.large;
			int name_max_w = layout.list_display_start_x + layout.list_display_size_x - elemHeight / 2 - text_x;
			char name_fit[sizeof(rom_name)];
			GFX_truncateText(nameFont, rom_name, name_fit, name_max_w, 0);
			renderText(name_fit, nameFont, textColor, &(SDL_Rect){text_x, row_y + title_y, layout.list_display_size_x, textHeight});
		}

		serializeTime(total, entry->play_time_total);
		serializeTime(average, entry->play_time_average);
		snprintf(plays, sizeof(plays), "%d", entry->play_count);

		// values first in the accent color, lowercase labels in muted gray
		const char* details[] = {total, " total  ·  ", average, " avg  ·  ", plays, entry->play_count == 1 ? " play" : " plays"};
		SDL_Rect detailsRect = {text_x, row_y + detail_y, layout.list_display_size_x, textHeight};
		// accent only reads well on the selected white pill; use light gray on dark rows
		SDL_Color valueCol = isSelected ? uintToColour(THEME_COLOR2_255) : COLOR_LIGHT_TEXT;
		// fetched right before use (a UIFont pointer is only valid until the next UIFont_get)
		TTF_Font* detailFont = UI_textRole(UI_TEXT_SECONDARY, false);
		if (!detailFont)
			detailFont = font.small;
		// clipped like the name: the segment that crosses the limit is ellipsised and the rest dropped
		int details_end = layout.list_display_start_x + layout.list_display_size_x - elemHeight / 2;
		for (int i = 0; i < 6; i++) {
			SDL_Color detailCol = i % 2 == 0 ? valueCol : COLOR_DARK_TEXT;
			int room = details_end - detailsRect.x;
			int seg_w = 0;
			GFX_measureText(detailFont, details[i], &seg_w, NULL);
			if (seg_w <= room) {
				detailsRect.x += renderText(details[i], detailFont, detailCol, &detailsRect);
				continue;
			}
			char seg_fit[64];
			snprintf(seg_fit, sizeof(seg_fit), "%s", details[i]);
			if (room > 0 && GFX_truncateText(detailFont, details[i], seg_fit, room, 0) <= room)
				renderText(seg_fit, detailFont, detailCol, &detailsRect);
			break;
		}
	}

	if (count > layout.items_per_page)
		UI_renderScrollArrows(screen, layout.up_y, layout.down_y, start > 0, end < count);
}

void initLayout() {
	// unscaled
	int hw = screen->w;
	int hh = screen->h;

	// the title. just leave the default padding all around
	layout.sub_title_x = SCALE1(PADDING);
	layout.sub_title_y = SCALE1(PADDING);

	// the main list (LIST-LAYOUT §10.1/§10.2): a rich list, so the thumbnail's left edge sits on the list text
	// start (14 dp, the title's x) with the capsule its image margin left of that; the row ends PADDING short
	// of the right edge. Rows sit in the standard block: equal half-row arrow strips above and below, centred
	// between the title's letters and the hint icons.
	int row_h = SCALE1(BIG_PILL_SIZE);
	UIListBlock block = UI_listBlock(UI_pageTitleBandTop(), UI_buttonHintIconTop(hh), row_h, 0);
	layout.list_display_start_x = UI_listTextX() - SCALE1(IMG_MARGIN) / 2;
	if (layout.list_display_start_x < 0)
		layout.list_display_start_x = 0;
	layout.list_display_start_y = block.top;
	layout.list_display_size_x = hw - SCALE1(PADDING) - layout.list_display_start_x;
	layout.list_display_size_y = block.rows * row_h;
	layout.up_y = block.up_y;
	layout.down_y = block.down_y;

	layout.list_display_rect.x = layout.list_display_start_x,
	layout.list_display_rect.y = layout.list_display_start_y,
	layout.list_display_rect.w = layout.list_display_size_x,
	layout.list_display_rect.h = layout.list_display_size_y;

	layout.items_per_page = block.rows > 0 ? block.rows : 1;
	layout.num_pages = (int)ceil((double)play_activities->count / (double)layout.items_per_page);
}

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;
	screen = GFX_init(MODE_MAIN);
	UI_showSplashScreen(screen, "Game Time");

	InitSettings();
	PAD_init();
	PWR_init();

	setup_signal_handlers();

	play_activities = play_activity_find_all();
	LOG_debug("found %d roms\n", play_activities->count);

	int count = play_activities->count;

	initLayout();
	preloadRomImages();
	int selected = 0;
	int start = 0;
	int end = MIN(count, layout.items_per_page);

	bool confirm_delete = false;
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;
	while (!app_quit) {
		GFX_startFrame();
		PAD_poll();
		// MENU + SELECT exits (same combo as in-game and the other pak tools).
		bool req_quit = false;
		UI_handleQuitRequest(screen, &req_quit, &dirty, "Exit Game Tracker?", NULL);
		if (req_quit)
			app_quit = true;

		if (confirm_delete) {
			if (PAD_justPressed(BTN_A)) {
				play_activity_delete(play_activities->play_activity[selected]->rom->id);

				// reload the list; free images before the activities they index
				freeRomImages();
				free_play_activities(play_activities);
				play_activities = play_activity_find_all();
				count = play_activities->count;
				preloadRomImages();
				initLayout();

				if (selected >= count)
					selected = MAX(0, count - 1);
				start = MIN(start, MAX(0, count - layout.items_per_page));
				if (selected < start)
					start = selected;
				end = MIN(count, start + layout.items_per_page);

				confirm_delete = false;
				dirty = true;
			} else if (PAD_justPressed(BTN_B)) {
				confirm_delete = false;
				dirty = true;
			}
		} else if (count > 0) {
			if (PAD_justRepeated(BTN_UP)) {
				selected -= 1;
				if (selected < 0) {
					selected = count - 1;
					start = MAX(0, count - layout.items_per_page);
					end = count;
				} else if (selected < start) {
					start -= 1;
					end -= 1;
				}
				dirty = true;
			} else if (PAD_justRepeated(BTN_DOWN)) {
				selected += 1;
				if (selected >= count) {
					selected = 0;
					start = 0;
					end = MIN(count, layout.items_per_page);
				} else if (selected >= end) {
					start += 1;
					end += 1;
				}
				dirty = true;
			} else if (PAD_justPressed(BTN_X)) {
				confirm_delete = true;
				dirty = true;
			} else if (PAD_justPressed(BTN_B)) {
				app_quit = true;
			}
		} else if (PAD_justPressed(BTN_B)) {
			app_quit = true;
		}

		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		if (dirty) {
			GFX_clear(screen);

			if (count == 0) {
				UI_renderMenuBar(screen, "Game Time");
				UI_renderEmptyState(screen, "No play activity", "Play some games to track your time", NULL);
			} else {
				char play_time_total_formatted[255];
				serializeTime(play_time_total_formatted, play_activities->play_time_total);
				char title[256];
				snprintf(title, sizeof(title), "Game Time: %s", play_time_total_formatted);
				UI_renderMenuBar(screen, title);

				renderList(count, start, end, selected);

				UI_renderButtonHintBar(screen, (char*[]){"B", "EXIT", "X", "DELETE", NULL});

				if (confirm_delete) {
					char rom_name[255];
					cleanName(rom_name, play_activities->play_activity[selected]->rom->name);
					UI_renderConfirmDialog(screen, "Delete Record?", rom_name);
				}
			}

			GFX_flip(screen);
			dirty = false;
		} else
			GFX_sync();
	}

	freeRomImages();
	free_play_activities(play_activities);

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();

	return EXIT_SUCCESS;
}