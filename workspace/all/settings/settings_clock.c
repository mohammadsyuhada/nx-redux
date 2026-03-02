// loosely based on https://github.com/gameblabla/clock_sdl_app

#include "settings_clock.h"
#include "defines.h"
#include "api.h"
#include "ui_components.h"
#include "utils.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

// ============================================
// Constants
// ============================================

enum {
	CURSOR_YEAR,
	CURSOR_MONTH,
	CURSOR_DAY,
	CURSOR_HOUR,
	CURSOR_MINUTE,
	CURSOR_SECOND,
	CURSOR_AMPM,
};

#define DIGIT_WIDTH 10
#define DIGIT_HEIGHT 16

#define CHAR_SLASH 10
#define CHAR_COLON 11

// ============================================
// Globals
// ============================================

static SDL_Surface* clock_digits;

static int32_t day_selected;
static int32_t month_selected;
static uint32_t year_selected;
static int32_t hour_selected;
static int32_t minute_selected;
static int32_t seconds_selected;
static int32_t am_selected;

// ============================================
// Rendering helpers
// ============================================

static int clock_blit(SDL_Surface* screen, int i, int x, int y) {
	SDL_BlitSurface(clock_digits, &(SDL_Rect){i * SCALE1(10), 0, SCALE2(10, 16)}, screen, &(SDL_Rect){x, y});
	return x + SCALE1(10);
}

static void clock_blitBar(SDL_Surface* screen, int x, int y, int w) {
	GFX_blitPill(ASSET_UNDERLINE, screen, &(SDL_Rect){x, y, w});
}

static int clock_blitNumber(SDL_Surface* screen, int num, int x, int y) {
	int n;
	if (num > 999) {
		n = num / 1000;
		num -= n * 1000;
		x = clock_blit(screen, n, x, y);

		n = num / 100;
		num -= n * 100;
		x = clock_blit(screen, n, x, y);
	}
	n = num / 10;
	num -= n * 10;
	x = clock_blit(screen, n, x, y);

	n = num;
	x = clock_blit(screen, n, x, y);

	return x;
}

// ============================================
// Validation
// ============================================

static void clock_validate(void) {
	// leap year
	uint32_t february_days = 28;
	if (((year_selected % 4 == 0) && (year_selected % 100 != 0)) || (year_selected % 400 == 0))
		february_days = 29;

	if (month_selected > 12)
		month_selected -= 12;
	else if (month_selected < 1)
		month_selected += 12;

	if (year_selected > 2100)
		year_selected = 2100;
	else if (year_selected < 1970)
		year_selected = 1970;

	switch (month_selected) {
	case 2:
		if (day_selected > (int32_t)february_days)
			day_selected -= february_days;
		else if (day_selected < 1)
			day_selected += february_days;
		break;
	case 4:
	case 6:
	case 9:
	case 11:
		if (day_selected > 30)
			day_selected -= 30;
		else if (day_selected < 1)
			day_selected += 30;
		break;
	default:
		if (day_selected > 31)
			day_selected -= 31;
		else if (day_selected < 1)
			day_selected += 31;
		break;
	}

	// time
	if (hour_selected > 23)
		hour_selected -= 24;
	else if (hour_selected < 0)
		hour_selected += 24;
	if (minute_selected > 59)
		minute_selected -= 60;
	else if (minute_selected < 0)
		minute_selected += 60;
	if (seconds_selected > 59)
		seconds_selected -= 60;
	else if (seconds_selected < 0)
		seconds_selected += 60;
}

// ============================================
// Clock adjustment main loop
// ============================================

void clock_adjustment_run(SDL_Surface* screen) {
	// Build digit sprite sheet
	clock_digits = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, SCALE1(120), SCALE1(16), 32, screen->format->format);
	SDL_FillRect(clock_digits, NULL, RGB_BLACK);

	SDL_Surface* digit;
	char* chars[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "/", ":", NULL};
	char* c;
	int i = 0;
	while ((c = chars[i])) {
		digit = TTF_RenderUTF8_Blended(font.large, c, COLOR_WHITE);
		int y = i == CHAR_COLON ? SCALE1(-1.5) : 0;
		SDL_BlitSurface(digit, NULL, clock_digits, &(SDL_Rect){(i * SCALE1(DIGIT_WIDTH)) + (SCALE1(DIGIT_WIDTH) - digit->w) / 2, y + (SCALE1(DIGIT_HEIGHT) - digit->h) / 2});
		SDL_FreeSurface(digit);
		i += 1;
	}

	int save_changes = 0;
	int select_cursor = 0;
	int show_24hour = exists(USERDATA_PATH "/show_24hour");

	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	day_selected = tm.tm_mday;
	month_selected = tm.tm_mon + 1;
	year_selected = tm.tm_year + 1900;
	hour_selected = tm.tm_hour;
	minute_selected = tm.tm_min;
	seconds_selected = tm.tm_sec;
	am_selected = tm.tm_hour < 12;

	int option_count = (show_24hour ? CURSOR_SECOND : CURSOR_AMPM) + 1;

	bool quit = false;
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!quit && !app_quit) {
		GFX_startFrame();
		PAD_poll();

		if (PAD_justRepeated(BTN_UP)) {
			dirty = true;
			switch (select_cursor) {
			case CURSOR_YEAR:
				year_selected++;
				break;
			case CURSOR_MONTH:
				month_selected++;
				break;
			case CURSOR_DAY:
				day_selected++;
				break;
			case CURSOR_HOUR:
				hour_selected++;
				break;
			case CURSOR_MINUTE:
				minute_selected++;
				break;
			case CURSOR_SECOND:
				seconds_selected++;
				break;
			case CURSOR_AMPM:
				hour_selected += 12;
				break;
			default:
				break;
			}
		} else if (PAD_justRepeated(BTN_DOWN)) {
			dirty = true;
			switch (select_cursor) {
			case CURSOR_YEAR:
				year_selected--;
				break;
			case CURSOR_MONTH:
				month_selected--;
				break;
			case CURSOR_DAY:
				day_selected--;
				break;
			case CURSOR_HOUR:
				hour_selected--;
				break;
			case CURSOR_MINUTE:
				minute_selected--;
				break;
			case CURSOR_SECOND:
				seconds_selected--;
				break;
			case CURSOR_AMPM:
				hour_selected -= 12;
				break;
			default:
				break;
			}
		} else if (PAD_justRepeated(BTN_LEFT)) {
			dirty = true;
			select_cursor--;
			if (select_cursor < 0)
				select_cursor += option_count;
		} else if (PAD_justRepeated(BTN_RIGHT)) {
			dirty = true;
			select_cursor++;
			if (select_cursor >= option_count)
				select_cursor -= option_count;
		} else if (PAD_justPressed(BTN_A)) {
			save_changes = 1;
			quit = true;
		} else if (PAD_justPressed(BTN_B)) {
			quit = true;
		} else if (PAD_justPressed(BTN_SELECT)) {
			dirty = true;
			show_24hour = !show_24hour;
			option_count = (show_24hour ? CURSOR_SECOND : CURSOR_AMPM) + 1;
			if (select_cursor >= option_count)
				select_cursor -= option_count;

			if (show_24hour) {
				system("touch " USERDATA_PATH "/show_24hour");
			} else {
				system("rm " USERDATA_PATH "/show_24hour");
			}
		}

		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		if (dirty) {
			clock_validate();

			GFX_clear(screen);

			UI_renderMenuBar(screen, "Clock");

			UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", "A", "SET", "SELECT", show_24hour ? "12 HOUR" : "24 HOUR", NULL});

			int ox = (screen->w - (show_24hour ? SCALE1(188) : SCALE1(223))) / 2;

			// datetime
			int x = ox;
			int y = SCALE1((((FIXED_HEIGHT / FIXED_SCALE) - PILL_SIZE - DIGIT_HEIGHT) / 2));

			x = clock_blitNumber(screen, year_selected, x, y);
			x = clock_blit(screen, CHAR_SLASH, x, y);
			x = clock_blitNumber(screen, month_selected, x, y);
			x = clock_blit(screen, CHAR_SLASH, x, y);
			x = clock_blitNumber(screen, day_selected, x, y);
			x += SCALE1(10); // space

			am_selected = hour_selected < 12;
			if (show_24hour) {
				x = clock_blitNumber(screen, hour_selected, x, y);
			} else {
				int hour = hour_selected;
				if (hour == 0)
					hour = 12;
				else if (hour > 12)
					hour -= 12;
				x = clock_blitNumber(screen, hour, x, y);
			}
			x = clock_blit(screen, CHAR_COLON, x, y);
			x = clock_blitNumber(screen, minute_selected, x, y);
			x = clock_blit(screen, CHAR_COLON, x, y);
			x = clock_blitNumber(screen, seconds_selected, x, y);

			int ampm_w = 0;
			if (!show_24hour) {
				x += SCALE1(10); // space
				SDL_Surface* text = TTF_RenderUTF8_Blended(font.large, am_selected ? "AM" : "PM", COLOR_WHITE);
				ampm_w = text->w + SCALE1(2);
				SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){x, y - SCALE1(3)});
				SDL_FreeSurface(text);
			}

			// cursor
			x = ox;
			y += SCALE1(19);
			if (select_cursor != CURSOR_YEAR) {
				x += SCALE1(50); // YYYY/
				x += (select_cursor - 1) * SCALE1(30);
			}
			clock_blitBar(screen, x, y, (select_cursor == CURSOR_YEAR ? SCALE1(40) : (select_cursor == CURSOR_AMPM ? ampm_w : SCALE1(20))));

			GFX_flip(screen);
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	SDL_FreeSurface(clock_digits);

	if (save_changes)
		PLAT_setDateTime(year_selected, month_selected, day_selected, hour_selected, minute_selected, seconds_selected);
}
