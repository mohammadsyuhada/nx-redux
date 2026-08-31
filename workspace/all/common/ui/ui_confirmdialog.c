#include "ui_confirmdialog.h"
#include "ui_draw.h"
#include "ui_pindialog.h"
#include "api.h"
#include "defines.h"

#include <string.h>
#include <stdio.h>

#define CONFIRM_MAX_SUB_LINES 4

// Greedy word-wrap `text` to `max_w` using `font`, into up to
// CONFIRM_MAX_SUB_LINES lines. Returns the line count. (SDL_ttf 2.0.13's
// wrapped rendering left-aligns lines, so callers render each line centered.)
static int confirm_wrap_lines(TTF_Font* font, const char* text, int max_w,
							  char lines[CONFIRM_MAX_SUB_LINES][256]) {
	char work[512];
	snprintf(work, sizeof(work), "%s", text);

	int n = 0;
	lines[0][0] = '\0';
	char* save = NULL;
	for (char* tok = strtok_r(work, " ", &save); tok && n < CONFIRM_MAX_SUB_LINES;
		 tok = strtok_r(NULL, " ", &save)) {
		char cand[256];
		snprintf(cand, sizeof(cand), "%s%s%s", lines[n], lines[n][0] ? " " : "", tok);
		int w = 0;
		GFX_measureText(font, cand, &w, NULL);
		if (w <= max_w || !lines[n][0]) {
			snprintf(lines[n], sizeof(lines[n]), "%s", cand);
		} else if (n + 1 < CONFIRM_MAX_SUB_LINES) {
			n++;
			snprintf(lines[n], sizeof(lines[n]), "%s", tok);
		} else {
			break; // out of lines; drop the rest
		}
	}
	return lines[0][0] ? n + 1 : 0;
}

void UI_renderConfirmDialog(SDL_Surface* dst, const char* title,
							const char* subtitle) {
	int padding_x = SCALE1(PADDING * 4);
	int content_w = dst->w - padding_x * 2;

	GFX_clearLayers(LAYER_SCROLLTEXT);
	SDL_FillRect(dst, NULL, SDL_MapRGB(dst->format, 0, 0, 0));

	int btn_sz = SCALE1(BUTTON_SIZE);

	// Wrap the subtitle ourselves into centered lines so a long subtitle
	// (a) leaves room for the buttons below it and (b) stays horizontally
	// centered - SDL_ttf's own wrapped rendering left-aligns.
	char sub_lines[CONFIRM_MAX_SUB_LINES][256];
	int sub_line_count = 0;
	int sub_line_h = TTF_FontHeight(font.small);
	if (subtitle && subtitle[0])
		sub_line_count = confirm_wrap_lines(font.small, subtitle, content_w, sub_lines);

	int title_h = TTF_FontHeight(font.large);
	int total_h = title_h;
	if (sub_line_count)
		total_h += SCALE1(BUTTON_MARGIN) + sub_line_count * sub_line_h;
	total_h += SCALE1(BUTTON_MARGIN) + btn_sz;

	int y = (dst->h - total_h) / 2;

	// Title - single-line row (unlike the subtitle, which wraps): truncate
	// with an ellipsis so a long title can't overflow into the subtitle/hint
	// row below.
	char title_buf[256];
	GFX_truncateText(font.large, title, title_buf, content_w, 0);
	SDL_Rect title_rect = {padding_x, y, content_w, title_h};
	GFX_blitMessage(font.large, title_buf, dst, &title_rect);
	y += title_h;

	// Subtitle (optional) - each line rendered horizontally centered
	if (sub_line_count) {
		y += SCALE1(BUTTON_MARGIN);
		for (int i = 0; i < sub_line_count; i++) {
			SDL_Surface* s = GFX_renderText(font.small, sub_lines[i], COLOR_WHITE);
			if (s) {
				SDL_BlitSurface(s, NULL, dst, &(SDL_Rect){(dst->w - s->w) / 2, y});
				SDL_FreeSurface(s);
			}
			y += sub_line_h;
		}
	}

	// Buttons
	y += SCALE1(BUTTON_MARGIN);
	UI_renderCenteredButtons(dst, y, (char*[]){"B", "CANCEL", "A", "CONFIRM", NULL});
}

int UI_modalLoop(const UI_ModalOpts* o) {
	int result = -1;
	bool dirty = true;
	uint32_t start = SDL_GetTicks();
	if (o->clear_layers)
		GFX_clearLayers(LAYER_ALL);
	while (1) {
		if (o->quit_flag && *o->quit_flag)
			break;
		if (o->timeout_ms && SDL_GetTicks() - start >= o->timeout_ms)
			break;
		GFX_startFrame();
		PAD_poll();
		int r = o->handle(o->ctx);
		if (r >= 0) {
			result = r;
			break;
		}
		if (r == UI_MODAL_DIRTY)
			dirty = true;
		// keep auto-sleep and the power button alive while the modal blocks
		PWR_update(&dirty, NULL, NULL, NULL);
		if (dirty) {
			o->render(o->screen, o->ctx);
			GFX_flip(o->screen);
			dirty = false;
		} else {
			GFX_delay();
		}
	}
	if (o->clear_layers)
		GFX_clearLayers(LAYER_ALL);
	if (o->reset_pad) {
		PAD_poll();
		PAD_reset();
	}
	return result;
}

typedef struct {
	const char* title;
	const char* subtitle;
} UI_ConfirmModalCtx;

static void confirmModal_render(SDL_Surface* screen, void* vctx) {
	UI_ConfirmModalCtx* ctx = vctx;
	UI_renderConfirmDialog(screen, ctx->title, ctx->subtitle);
}

static int confirmModal_handle(void* vctx) {
	(void)vctx;
	if (PAD_justPressed(BTN_A))
		return 1;
	if (PAD_justPressed(BTN_B))
		return 0;
	return UI_MODAL_CONTINUE;
}

bool UI_confirmModal(SDL_Surface* screen, const char* title, const char* subtitle,
					 const volatile bool* quit_flag, bool clear_layers, bool reset_pad) {
	UI_ConfirmModalCtx ctx = {title, subtitle};
	UI_ModalOpts opts = {
		.screen = screen,
		.render = confirmModal_render,
		.handle = confirmModal_handle,
		.ctx = &ctx,
		.quit_flag = quit_flag,
		.timeout_ms = 0,
		.clear_layers = clear_layers,
		.reset_pad = reset_pad,
	};
	return UI_modalLoop(&opts) == 1;
}

typedef struct {
	char* pin_out;
} UI_PinModalCtx;

static void pinModal_render(SDL_Surface* screen, void* vctx) {
	(void)vctx;
	PinDialog_render(screen);
}

static int pinModal_handle(void* vctx) {
	UI_PinModalCtx* ctx = vctx;
	PinDialogResult r = PinDialog_handleInput();
	if (r.action == PINDIALOG_CONFIRMED) {
		snprintf(ctx->pin_out, PINDIALOG_PIN_LEN + 1, "%s", r.pin);
		return 1;
	}
	if (r.action == PINDIALOG_CANCEL)
		return 0;
	// any held/pressed button may have changed a digit
	return PAD_anyPressed() ? UI_MODAL_DIRTY : UI_MODAL_CONTINUE;
}

bool UI_pinModal(SDL_Surface* screen, const char* title, const char* error, char* pin_out,
				 const volatile bool* quit_flag, bool clear_layers, bool reset_pad) {
	PinDialog_init(title);
	PinDialog_setError(error);
	UI_PinModalCtx ctx = {pin_out};
	UI_ModalOpts opts = {
		.screen = screen,
		.render = pinModal_render,
		.handle = pinModal_handle,
		.ctx = &ctx,
		.quit_flag = quit_flag,
		.timeout_ms = 0,
		.clear_layers = clear_layers,
		.reset_pad = reset_pad,
	};
	bool confirmed = UI_modalLoop(&opts) == 1;
	PinDialog_quit();
	return confirmed;
}
