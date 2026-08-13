#include "ui_draw.h"
#include "api.h"
#include "defines.h"
#include <math.h>

static uint32_t px_get(SDL_Surface* s, int x, int y) {
	uint8_t* p = (uint8_t*)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
	switch (s->format->BytesPerPixel) {
	case 2:
		return *(uint16_t*)p;
	case 4:
		return *(uint32_t*)p;
	default:
		return *p;
	}
}

static void px_put(SDL_Surface* s, int x, int y, uint32_t pixel) {
	uint8_t* p = (uint8_t*)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
	switch (s->format->BytesPerPixel) {
	case 2:
		*(uint16_t*)p = (uint16_t)pixel;
		break;
	case 4:
		*(uint32_t*)p = pixel;
		break;
	default:
		*p = (uint8_t)pixel;
		break;
	}
}

// Anti-alias one r×r corner quadrant: for each pixel, coverage = how far its
// centre sits inside the arc of radius r about (ccx,ccy); blend `color` into the
// destination by that coverage so the curve fades smoothly instead of stepping.
static void aa_corner(SDL_Surface* dst, int px0, int py0, float ccx, float ccy,
					  int r, uint8_t cr, uint8_t cg, uint8_t cb) {
	for (int py = py0; py < py0 + r; py++) {
		if (py < 0 || py >= dst->h)
			continue;
		for (int px = px0; px < px0 + r; px++) {
			if (px < 0 || px >= dst->w)
				continue;
			float dx = (px + 0.5f) - ccx;
			float dy = (py + 0.5f) - ccy;
			float cov = (float)r - sqrtf(dx * dx + dy * dy) + 0.5f;
			if (cov <= 0.0f)
				continue;
			if (cov >= 1.0f) {
				px_put(dst, px, py, SDL_MapRGB(dst->format, cr, cg, cb));
				continue;
			}
			uint8_t dr, dg, db;
			SDL_GetRGB(px_get(dst, px, py), dst->format, &dr, &dg, &db);
			uint8_t br = (uint8_t)(cr * cov + dr * (1.0f - cov) + 0.5f);
			uint8_t bg = (uint8_t)(cg * cov + dg * (1.0f - cov) + 0.5f);
			uint8_t bb = (uint8_t)(cb * cov + db * (1.0f - cov) + 0.5f);
			px_put(dst, px, py, SDL_MapRGB(dst->format, br, bg, bb));
		}
	}
}

void UI_fillRoundedRect(SDL_Surface* dst, int x, int y, int w, int h,
						int radius, uint32_t color) {
	int r = radius;
	if (r > w / 2)
		r = w / 2;
	if (r > h / 2)
		r = h / 2;
	if (r < 0)
		r = 0;

	// Solid interior + straight edges (axis-aligned, no AA needed): a middle band
	// the full height between the corners, plus the top/bottom strips between them.
	if (h - 2 * r > 0)
		SDL_FillRect(dst, &(SDL_Rect){x, y + r, w, h - 2 * r}, color);
	if (r > 0 && w - 2 * r > 0) {
		SDL_FillRect(dst, &(SDL_Rect){x + r, y, w - 2 * r, r}, color);
		SDL_FillRect(dst, &(SDL_Rect){x + r, y + h - r, w - 2 * r, r}, color);
	}
	if (r == 0) {
		if (h - 2 * r <= 0)
			SDL_FillRect(dst, &(SDL_Rect){x, y, w, h}, color);
		return;
	}

	uint8_t cr, cg, cb;
	SDL_GetRGB(color, dst->format, &cr, &cg, &cb);
	if (SDL_MUSTLOCK(dst))
		SDL_LockSurface(dst);
	aa_corner(dst, x, y, x + r, y + r, r, cr, cg, cb);						   // top-left
	aa_corner(dst, x + w - r, y, x + w - r, y + r, r, cr, cg, cb);			   // top-right
	aa_corner(dst, x, y + h - r, x + r, y + h - r, r, cr, cg, cb);			   // bottom-left
	aa_corner(dst, x + w - r, y + h - r, x + w - r, y + h - r, r, cr, cg, cb); // bottom-right
	if (SDL_MUSTLOCK(dst))
		SDL_UnlockSurface(dst);
}

SDL_Surface* UI_getScrim(SDL_Surface** cache, int w, int h) {
	SDL_Surface* scrim = *cache;
	if (!scrim || scrim->w != w || scrim->h != h) {
		if (scrim)
			SDL_FreeSurface(scrim);
		scrim = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32,
									 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
		if (scrim) {
			SDL_FillRect(scrim, NULL, SDL_MapRGBA(scrim->format, 0, 0, 0, 178));
			SDL_SetSurfaceBlendMode(scrim, SDL_BLENDMODE_BLEND);
		}
		*cache = scrim;
	}
	return scrim;
}

void UI_renderCenteredButtons(SDL_Surface* dst, int y, char** pairs) {
	int btn_sz = SCALE1(BUTTON_SIZE);
	int btn_gap = SCALE1(BUTTON_TEXT_GAP);
	int btn_margin = SCALE1(BUTTON_MARGIN);

	// Measure the row
	int widths[8];
	int count = 0;
	int total_w = 0;
	for (int i = 0; pairs[i * 2] && pairs[i * 2 + 1] && count < 8; i++) {
		int text_w, th;
		GFX_measureText(font.tiny, pairs[i * 2 + 1], &text_w, &th);
		widths[count] = btn_sz + btn_gap + text_w;
		total_w += (count > 0 ? btn_margin : 0) + widths[count];
		count++;
	}
	if (count == 0)
		return;

	// Render centered
	int bx = (dst->w - total_w) / 2;
	for (int i = 0; i < count; i++) {
		GFX_blitButton(pairs[i * 2 + 1], pairs[i * 2], dst, &(SDL_Rect){bx, y, 0, 0});
		bx += widths[i] + btn_margin;
	}
}
