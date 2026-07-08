#include "ui_buttonhintbar.h"
#include "ui_draw.h"
#include "api.h"
#include "defines.h"

int UI_renderButtonHintBar(SDL_Surface* dst, char** pairs) {
	IndicatorType show_setting = PWR_getShowSetting();
	char** hw_pairs = show_setting ? GFX_getHardwareHintPairs(show_setting) : NULL;

	struct Hint {
		char* hint;
		char* button;
		int ow;
	};

	struct Hint hints[4];
	int count = 0;
	int total_w = 0;

	// Parse hardware hints first (priority), then caller pairs
	char** groups[] = {hw_pairs, pairs};
	for (int g = 0; g < 2; g++) {
		if (!groups[g])
			continue;
		for (int i = 0; groups[g][i * 2] && count < 4; i++) {
			char* button = groups[g][i * 2];
			char* hint = groups[g][i * 2 + 1];
			if (!hint)
				break;
			int w = GFX_getButtonWidth(hint, button);
			hints[count++] = (struct Hint){hint, button, w};
			total_w += SCALE1(BUTTON_MARGIN) + w;
		}
	}

	if (count == 0)
		return 0;
	total_w += SCALE1(BUTTON_MARGIN);

	// Full-width semi-transparent black bar
	int btn_sz = SCALE1(BUTTON_SIZE);
	int bar_h = btn_sz + SCALE1(BUTTON_MARGIN * 2);
	int oy = dst->h - bar_h;

	static SDL_Surface* button_bar = NULL;
	if (!UI_getScrim(&button_bar, dst->w, bar_h))
		return 0;
	SDL_BlitSurface(button_bar, NULL, dst, &(SDL_Rect){0, oy});

	// Render all buttons from the left
	int by = oy + (bar_h - btn_sz) / 2;
	int ox = SCALE1(PADDING) + SCALE1(BUTTON_MARGIN);
	for (int i = 0; i < count; i++) {
		GFX_blitButton(hints[i].hint, hints[i].button, dst, &(SDL_Rect){ox, by});
		ox += hints[i].ow + SCALE1(BUTTON_MARGIN);
	}

	return total_w;
}
