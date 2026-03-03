#include "scraper_compositor.h"
#include "utils.h"
#include "api.h"
#include <stdio.h>
#include <string.h>

#define CANVAS_W 640
#define CANVAS_H 480

// Scale surface to fit within max_w x max_h, maintaining aspect ratio
static SDL_Surface* scaleSurface(SDL_Surface* src, int max_w, int max_h) {
	if (!src)
		return NULL;

	double scale_x = (double)max_w / src->w;
	double scale_y = (double)max_h / src->h;
	double scale = (scale_x < scale_y) ? scale_x : scale_y;

	int new_w = (int)(src->w * scale);
	int new_h = (int)(src->h * scale);
	if (new_w < 1)
		new_w = 1;
	if (new_h < 1)
		new_h = 1;

	SDL_Surface* dst = SDL_CreateRGBSurface(0, new_w, new_h, 32,
											0x00FF0000, 0x0000FF00,
											0x000000FF, 0xFF000000);
	if (!dst)
		return NULL;

	// Disable blend mode during scaling to avoid premultiplied alpha artifacts
	SDL_BlendMode prev;
	SDL_GetSurfaceBlendMode(src, &prev);
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_BlitScaled(src, NULL, dst, NULL);
	SDL_SetSurfaceBlendMode(src, prev);
	return dst;
}

// Scale surface to fill the entire target area (crop to fit)
static SDL_Surface* scaleSurfaceToFill(SDL_Surface* src, int target_w, int target_h) {
	if (!src)
		return NULL;

	double scale_x = (double)target_w / src->w;
	double scale_y = (double)target_h / src->h;
	double scale = (scale_x > scale_y) ? scale_x : scale_y;

	int scaled_w = (int)(src->w * scale);
	int scaled_h = (int)(src->h * scale);
	if (scaled_w < 1)
		scaled_w = 1;
	if (scaled_h < 1)
		scaled_h = 1;

	SDL_Surface* scaled = SDL_CreateRGBSurface(0, scaled_w, scaled_h, 32,
											   0x00FF0000, 0x0000FF00,
											   0x000000FF, 0xFF000000);
	if (!scaled)
		return NULL;
	SDL_BlendMode prev;
	SDL_GetSurfaceBlendMode(src, &prev);
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_BlitScaled(src, NULL, scaled, NULL);
	SDL_SetSurfaceBlendMode(src, prev);

	// Crop to center
	SDL_Surface* dst = SDL_CreateRGBSurface(0, target_w, target_h, 32,
											0x00FF0000, 0x0000FF00,
											0x000000FF, 0xFF000000);
	if (!dst) {
		SDL_FreeSurface(scaled);
		return NULL;
	}

	SDL_Rect src_rect = {
		(scaled_w - target_w) / 2,
		(scaled_h - target_h) / 2,
		target_w, target_h};
	SDL_BlitSurface(scaled, &src_rect, dst, NULL);
	SDL_FreeSurface(scaled);
	return dst;
}

// Draw a shape-following drop shadow using the image's alpha channel
static void drawShapeShadow(SDL_Surface* canvas, SDL_Surface* shape,
							SDL_Rect* rect, int offset, int alpha) {
	SDL_Surface* shadow = SDL_CreateRGBSurface(0, shape->w, shape->h, 32,
											   0x00FF0000, 0x0000FF00,
											   0x000000FF, 0xFF000000);
	if (!shadow)
		return;

	// Copy shape to get its alpha channel
	SDL_SetSurfaceBlendMode(shape, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(shape, NULL, shadow, NULL);
	SDL_SetSurfaceBlendMode(shape, SDL_BLENDMODE_BLEND);

	// Modulate to black with reduced alpha — only the alpha channel matters
	SDL_SetSurfaceColorMod(shadow, 0, 0, 0);
	SDL_SetSurfaceAlphaMod(shadow, alpha);

	SDL_Rect dst_rect = {rect->x + offset, rect->y + offset, shape->w, shape->h};
	SDL_SetSurfaceBlendMode(shadow, SDL_BLENDMODE_BLEND);
	SDL_BlitSurface(shadow, NULL, canvas, &dst_rect);
	SDL_FreeSurface(shadow);
}

SDL_Surface* Compositor_create(const char* screenshot_path,
							   const char* boxart_path,
							   const char* wheel_path) {
	// We need at least one image
	if (!screenshot_path && !boxart_path && !wheel_path)
		return NULL;

	// Create ARGB32 canvas
	SDL_Surface* canvas = SDL_CreateRGBSurface(0, CANVAS_W, CANVAS_H, 32,
											   0x00FF0000, 0x0000FF00,
											   0x000000FF, 0xFF000000);
	if (!canvas)
		return NULL;

	// Transparent background
	SDL_FillRect(canvas, NULL, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));

	int padding = 12;
	// Screenshot inset so box art and wheel float outside its edges
	int ss_pad_x = (int)(CANVAS_W * 0.06);		// left/right padding
	int ss_pad_bottom = (int)(CANVAS_H * 0.12); // bottom padding
	int ss_area_w = CANVAS_W - ss_pad_x * 2;
	int ss_area_h = CANVAS_H - ss_pad_bottom;

	// Layer 1: Screenshot (inset with padding, top-aligned)
	if (screenshot_path) {
		SDL_Surface* ss_raw = IMG_Load(screenshot_path);
		if (ss_raw) {
			// Convert to 32-bit ARGB (paletted/8-bit PNGs lose alpha during scaling)
			SDL_Surface* ss = SDL_ConvertSurface(ss_raw, canvas->format, 0);
			SDL_FreeSurface(ss_raw);
			if (ss) {
				SDL_Surface* bg = scaleSurfaceToFill(ss, ss_area_w, ss_area_h);
				if (bg) {
					SDL_Rect dst_rect = {ss_pad_x, 0, bg->w, bg->h};
					SDL_SetSurfaceBlendMode(bg, SDL_BLENDMODE_NONE);
					SDL_BlitSurface(bg, NULL, canvas, &dst_rect);
					SDL_FreeSurface(bg);
				}
				SDL_FreeSurface(ss);
			}
		}
	}

	// Layer 2: Box art (bottom-left, half canvas size)
	if (boxart_path) {
		SDL_Surface* box_raw = IMG_Load(boxart_path);
		if (box_raw) {
			SDL_Surface* box = SDL_ConvertSurface(box_raw, canvas->format, 0);
			SDL_FreeSurface(box_raw);
			if (box) {
				int max_w = (int)(CANVAS_W * 0.50);
				int max_h = (int)(CANVAS_H * 0.50);
				SDL_Surface* scaled = scaleSurface(box, max_w, max_h);
				if (scaled) {
					SDL_Rect box_rect = {
						padding,
						CANVAS_H - scaled->h - padding,
						scaled->w, scaled->h};
					drawShapeShadow(canvas, scaled, &box_rect, 3, 100);
					SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_BLEND);
					SDL_BlitSurface(scaled, NULL, canvas, &box_rect);
					SDL_FreeSurface(scaled);
				}
				SDL_FreeSurface(box);
			}
		}
	}

	// Layer 3: Wheel/logo (bottom-right, half canvas width)
	if (wheel_path) {
		SDL_Surface* wheel_raw = IMG_Load(wheel_path);
		if (wheel_raw) {
			SDL_Surface* wheel = SDL_ConvertSurface(wheel_raw, canvas->format, 0);
			SDL_FreeSurface(wheel_raw);
			if (wheel) {
				int max_w = (int)(CANVAS_W * 0.50);
				int max_h = (int)(CANVAS_H * 0.30);
				SDL_Surface* scaled = scaleSurface(wheel, max_w, max_h);
				if (scaled) {
					SDL_Rect wheel_rect = {
						CANVAS_W - scaled->w - padding,
						CANVAS_H - scaled->h - padding,
						scaled->w, scaled->h};
					drawShapeShadow(canvas, scaled, &wheel_rect, 2, 80);
					SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_BLEND);
					SDL_BlitSurface(scaled, NULL, canvas, &wheel_rect);
					SDL_FreeSurface(scaled);
				}
				SDL_FreeSurface(wheel);
			}
		}
	}

	return canvas;
}

bool Compositor_savePNG(SDL_Surface* surface, const char* path) {
	if (!surface || !path)
		return false;

	// Ensure parent directory exists
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	char* last_slash = strrchr(dir, '/');
	if (last_slash) {
		*last_slash = '\0';
		mkdir_p(dir);
	}

	SDL_RWops* rw = SDL_RWFromFile(path, "wb");
	if (!rw)
		return false;

	int ret = IMG_SavePNG_RW(surface, rw, 1);
	return ret == 0;
}
