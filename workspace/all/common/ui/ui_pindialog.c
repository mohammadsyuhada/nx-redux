#include "ui_pindialog.h"
#include "ui_draw.h"
#include "ui_menubar.h"
#include "ui_buttonhintbar.h"
#include "api.h"
#include "defines.h"
#include <stdio.h>
#include <string.h>

static char pin_title[128];
static char pin_error[128];
static int pin_digits[PINDIALOG_PIN_LEN];
static int pin_focus;

void PinDialog_init(const char* title) {
	strncpy(pin_title, title, sizeof(pin_title) - 1);
	pin_title[sizeof(pin_title) - 1] = '\0';
	pin_error[0] = '\0';
	memset(pin_digits, 0, sizeof(pin_digits));
	pin_focus = 0;
}

void PinDialog_setError(const char* msg) {
	if (msg) {
		strncpy(pin_error, msg, sizeof(pin_error) - 1);
		pin_error[sizeof(pin_error) - 1] = '\0';
	} else {
		pin_error[0] = '\0';
	}
}

PinDialogResult PinDialog_handleInput(void) {
	PinDialogResult result = {PINDIALOG_NONE, ""};

	if (PAD_justPressed(BTN_B)) {
		result.action = PINDIALOG_CANCEL;
		return result;
	}
	if (PAD_justPressed(BTN_A)) {
		result.action = PINDIALOG_CONFIRMED;
		for (int i = 0; i < PINDIALOG_PIN_LEN; i++)
			result.pin[i] = (char)('0' + pin_digits[i]);
		result.pin[PINDIALOG_PIN_LEN] = '\0';
		return result;
	}

	if (PAD_justRepeated(BTN_UP))
		pin_digits[pin_focus] = (pin_digits[pin_focus] + 1) % 10;
	else if (PAD_justRepeated(BTN_DOWN))
		pin_digits[pin_focus] = (pin_digits[pin_focus] + 9) % 10;
	else if (PAD_justRepeated(BTN_LEFT))
		pin_focus = (pin_focus + PINDIALOG_PIN_LEN - 1) % PINDIALOG_PIN_LEN;
	else if (PAD_justRepeated(BTN_RIGHT))
		pin_focus = (pin_focus + 1) % PINDIALOG_PIN_LEN;

	return result;
}

void PinDialog_render(SDL_Surface* screen) {
	GFX_clearLayers(LAYER_SCROLLTEXT);
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));

	UI_renderMenuBar(screen, pin_title);

	int slot_w = SCALE1(BUTTON_SIZE * 2);
	int slot_h = SCALE1(BUTTON_SIZE * 2);
	int gap = SCALE1(BUTTON_MARGIN * 2);
	int total_w = PINDIALOG_PIN_LEN * slot_w + (PINDIALOG_PIN_LEN - 1) * gap;
	int x = (screen->w - total_w) / 2;
	int y = (screen->h - slot_h) / 2 - SCALE1(BUTTON_SIZE);

	for (int i = 0; i < PINDIALOG_PIN_LEN; i++) {
		int focused = (i == pin_focus);
		UI_fillRoundedRect(screen, x, y, slot_w, slot_h, slot_h / 4,
						   focused ? THEME_COLOR1 : THEME_COLOR2);

		if (focused) {
			char glyph[2] = {(char)('0' + pin_digits[i]), '\0'};
			SDL_Surface* text = TTF_RenderUTF8_Blended(font.large, glyph, ALT_BUTTON_TEXT_COLOR);
			if (text) {
				SDL_BlitSurface(text, NULL, screen,
								&(SDL_Rect){x + (slot_w - text->w) / 2,
											y + (slot_h - text->h) / 2, 0, 0});
				SDL_FreeSurface(text);
			}

			SDL_Rect arrow;
			GFX_assetRect(ASSET_SCROLL_UP, &arrow);
			int ax = x + (slot_w - arrow.w) / 2;
			GFX_blitAsset(ASSET_SCROLL_UP, NULL, screen,
						  &(SDL_Rect){ax, y - SCALE1(BUTTON_MARGIN) - arrow.h, 0, 0});
			GFX_blitAsset(ASSET_SCROLL_DOWN, NULL, screen,
						  &(SDL_Rect){ax, y + slot_h + SCALE1(BUTTON_MARGIN), 0, 0});
		} else {
			// dot instead of the digit so an onlooker can't read the code
			int dot = SCALE1(8);
			UI_fillRoundedRect(screen, x + (slot_w - dot) / 2,
							   y + (slot_h - dot) / 2, dot, dot, dot / 2,
							   THEME_COLOR4);
		}
		x += slot_w + gap;
	}

	if (pin_error[0]) {
		SDL_Surface* err = TTF_RenderUTF8_Blended(font.small, pin_error, COLOR_GRAY);
		if (err) {
			SDL_BlitSurface(err, NULL, screen,
							&(SDL_Rect){(screen->w - err->w) / 2,
										y + slot_h + SCALE1(BUTTON_MARGIN * 2 + 24), 0, 0});
			SDL_FreeSurface(err);
		}
	}

	UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", "A", "CONFIRM", NULL});
}

void PinDialog_quit(void) {
	pin_title[0] = '\0';
	pin_error[0] = '\0';
	memset(pin_digits, 0, sizeof(pin_digits));
	pin_focus = 0;
}
