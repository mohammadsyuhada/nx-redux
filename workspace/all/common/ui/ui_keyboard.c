// In-process on-screen keyboard. Replaces the vendored prebuilt
// SYSTEM/shared/bin/keyboard binary: same bare-glyph black layout,
// themed selection cursor, standard NX Redux button hint bar.
#include "ui_keyboard.h"
#include "ui_draw.h"
#include "ui_buttonhintbar.h"
#include "api.h"
#include "config.h"
#include "defines.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KB_ROWS 6
#define KB_COLS 10
#define KB_MAX_INPUT 128

// Stock binary layout: number row, three QWERTY rows, symbols row,
// then the wide Space / Confirm row.
static const char* kb_lower[KB_ROWS][KB_COLS] = {
	{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
	{"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
	{"a", "s", "d", "f", "g", "h", "j", "k", "l", ";"},
	{"z", "x", "c", "v", "b", "n", "m", ",", ".", "/"},
	{"`", "'", "-", "=", "[", "]", "\\", NULL, NULL, NULL},
	{"Space", "Confirm", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}};

static const char* kb_upper[KB_ROWS][KB_COLS] = {
	{"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"},
	{"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"},
	{"A", "S", "D", "F", "G", "H", "J", "K", "L", ":"},
	{"Z", "X", "C", "V", "B", "N", "M", "<", ">", "?"},
	{"~", "\"", "_", "+", "{", "}", "|", NULL, NULL, NULL},
	{"Space", "Confirm", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}};

void UIKeyboard_init(void) {
	// No-op: nothing to initialize since the external binary was replaced
	// by this in-process implementation.
}

static int kb_row_len(const char* layout[KB_ROWS][KB_COLS], int row) {
	int n = 0;
	while (n < KB_COLS && layout[row][n])
		n++;
	return n;
}

static void kb_draw(SDL_Surface* screen, const char* title, const char* input,
					int cur_row, int cur_col, int shift) {
	const char*(*layout)[KB_COLS] = shift ? kb_upper : kb_lower;

	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));

	SDL_Surface* text;
	int center_x = screen->w / 2;
	SDL_Color focused_color = uintToColour(THEME_COLOR5_255);

	// Vertical layout: title, input line, 6 grid rows; centered as a block.
	int title_h = TTF_FontHeight(font.small);
	int input_h = TTF_FontHeight(font.large);
	int gap = SCALE1(8);
	// Reserve the bottom hint-bar band (same bar_h formula as ui_buttonhintbar.c)
	// and derive the row height from what's left, so 6 rows always fit above the
	// hint bar even on the Brick's short 768px panel. Clamp so proportions stay
	// sane on taller/low-scale screens.
	int hint_h = SCALE1(BUTTON_SIZE) + SCALE1(BUTTON_MARGIN * 2);
	int avail_h = screen->h - hint_h;
	int cell_h = (avail_h - title_h - input_h - gap * 3) / KB_ROWS;
	if (cell_h > SCALE1(44))
		cell_h = SCALE1(44);
	int total_h = title_h + gap + input_h + gap * 2 + KB_ROWS * cell_h;
	int y = (avail_h - total_h) / 2;

	// Title (the caller's prompt)
	if (title && title[0]) {
		text = GFX_renderText(font.small, title, COLOR_GRAY);
		if (text) {
			SDL_BlitSurface(text, NULL, screen,
							&(SDL_Rect){center_x - text->w / 2, y});
			SDL_FreeSurface(text);
		}
	}
	y += title_h + gap;

	// Static keyboard width referenced to the Brick (cap per-cell width so the
	// grid doesn't span the whole panel on wider screens like the Smart Pro).
	int cell_w = (screen->w - SCALE1(PADDING * 2)) / KB_COLS;
	if (cell_w > SCALE1(32))
		cell_w = SCALE1(32);
	int kb_w = KB_COLS * cell_w;
	int kb_left = center_x - kb_w / 2;

	// Typed text with caret; keep the tail visible when it overflows.
	char shown[KB_MAX_INPUT + 2];
	snprintf(shown, sizeof(shown), "%s_", input);
	text = GFX_renderText(font.large, shown, COLOR_WHITE);
	if (text) {
		int max_w = kb_w;
		if (text->w > max_w) {
			SDL_Rect src = {text->w - max_w, 0, max_w, text->h};
			SDL_BlitSurface(text, &src, screen,
							&(SDL_Rect){center_x - max_w / 2, y});
		} else {
			SDL_BlitSurface(text, NULL, screen,
							&(SDL_Rect){center_x - text->w / 2, y});
		}
		SDL_FreeSurface(text);
	}
	y += input_h + gap * 2;

	// Key grid: bare glyphs, no key boxes; the focused key gets a filled
	// THEME_COLOR1 circle (capsule for the wide Space/Confirm keys), drawn
	// with the anti-aliased rounded-rect primitive.
	for (int row = 0; row < KB_ROWS; row++) {
		int len = kb_row_len(layout, row);
		int cy = y + row * cell_h + cell_h / 2;

		if (row == KB_ROWS - 1) {
			// Space / Confirm: one wide key centered in each half.
			for (int col = 0; col < len; col++) {
				const char* key = layout[row][col];
				int cx = kb_left + (col == 0 ? kb_w / 4 : (kb_w * 3) / 4);
				bool focused = (row == cur_row && col == cur_col);
				text = GFX_renderText(font.large, key,
									  focused ? focused_color : COLOR_WHITE);
				if (focused) {
					// Draw the cursor before the glyph so a text-render failure
					// still shows the capsule (mirrors the narrow-key branch).
					int pw = (text ? text->w : SCALE1(48)) + SCALE1(14) * 2;
					int ph = SCALE1(26);
					if (ph > cell_h - SCALE1(2))
						ph = cell_h - SCALE1(2);
					UI_fillRoundedRect(screen, cx - pw / 2, cy - ph / 2,
									   pw, ph, ph / 2, THEME_COLOR1);
				}
				if (text) {
					SDL_BlitSurface(text, NULL, screen,
									&(SDL_Rect){cx - text->w / 2, cy - text->h / 2});
					SDL_FreeSurface(text);
				}
			}
		} else {
			int start_x = center_x - (len * cell_w) / 2;
			for (int col = 0; col < len; col++) {
				const char* key = layout[row][col];
				int cx = start_x + col * cell_w + cell_w / 2;
				bool focused = (row == cur_row && col == cur_col);
				if (focused) {
					int d = SCALE1(26);
					if (d > cell_h - SCALE1(2))
						d = cell_h - SCALE1(2);
					UI_fillRoundedRect(screen, cx - d / 2, cy - d / 2,
									   d, d, d / 2, THEME_COLOR1);
				}
				text = GFX_renderText(font.large, key,
									  focused ? focused_color : COLOR_WHITE);
				if (text) {
					SDL_BlitSurface(text, NULL, screen,
									&(SDL_Rect){cx - text->w / 2, cy - text->h / 2});
					SDL_FreeSurface(text);
				}
			}
		}
	}

	// Standard hint bar: left-aligned PNG glyph buttons.
	UI_renderButtonHintBar(screen, (char*[]){"Y", "DELETE", "X", "SHIFT",
											 "B", "EXIT", "A", "SELECT", NULL});

	GFX_flip(screen);
}

char* UIKeyboard_open(const char* prompt) {
	SDL_Surface* screen = GFX_getScreen();
	if (!screen) {
		LOG_error("UIKeyboard_open: no screen (GFX_init not called?)\n");
		return NULL;
	}

	char input[KB_MAX_INPUT + 1] = {0};
	int len = 0;
	int cur_row = 0, cur_col = 0;
	int shift = 0;
	bool dirty = true;

	// Don't let the button press that opened the keyboard leak in.
	PAD_reset();

	while (1) {
		GFX_startFrame();
		PAD_poll();

		const char*(*layout)[KB_COLS] = shift ? kb_upper : kb_lower;

		if (PAD_justPressed(BTN_B)) {
			PAD_reset();
			return NULL;
		}

		// BTN_UP/DOWN/LEFT/RIGHT are composites (BTN_DPAD_* | BTN_ANALOG_*),
		// so the left analog stick navigates too on devices that have one
		// (Brick Pro / Smart Pro / Smart Pro S) with repeat handled by
		// PAD_setAnalog in the input layer.
		if (PAD_justRepeated(BTN_UP)) {
			cur_row = (cur_row + KB_ROWS - 1) % KB_ROWS;
			int rl = kb_row_len(layout, cur_row);
			if (cur_col >= rl)
				cur_col = rl - 1;
			dirty = true;
		} else if (PAD_justRepeated(BTN_DOWN)) {
			cur_row = (cur_row + 1) % KB_ROWS;
			int rl = kb_row_len(layout, cur_row);
			if (cur_col >= rl)
				cur_col = rl - 1;
			dirty = true;
		} else if (PAD_justRepeated(BTN_LEFT)) {
			int rl = kb_row_len(layout, cur_row);
			cur_col = (cur_col + rl - 1) % rl;
			dirty = true;
		} else if (PAD_justRepeated(BTN_RIGHT)) {
			int rl = kb_row_len(layout, cur_row);
			cur_col = (cur_col + 1) % rl;
			dirty = true;
		} else if (PAD_justPressed(BTN_X)) {
			shift = !shift;
			dirty = true;
		} else if (PAD_justPressed(BTN_Y)) {
			if (len > 0) {
				input[--len] = '\0';
				dirty = true;
			}
		} else if (PAD_justPressed(BTN_A)) {
			const char* key = layout[cur_row][cur_col];
			if (strcmp(key, "Space") == 0) {
				if (len < KB_MAX_INPUT) {
					input[len++] = ' ';
					input[len] = '\0';
					dirty = true;
				}
			} else if (strcmp(key, "Confirm") == 0) {
				PAD_reset();
				if (len == 0)
					return NULL; // empty input = cancel (matches old wrapper)
				return strdup(input);
			} else {
				if (len < KB_MAX_INPUT) {
					input[len++] = key[0];
					input[len] = '\0';
					dirty = true;
				}
			}
		}

		PWR_update(&dirty, NULL, NULL, NULL);

		if (dirty) {
			kb_draw(screen, prompt, input, cur_row, cur_col, shift);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}
