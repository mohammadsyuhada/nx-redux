#include "scraper_compositor.h"
#include "utils.h"
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

// Draw a semi-transparent shadow behind a region
static void drawShadow(SDL_Surface* canvas, SDL_Rect* rect, int offset, int alpha) {
	SDL_Surface* shadow = SDL_CreateRGBSurface(0, rect->w + offset * 2,
											   rect->h + offset * 2, 32,
											   0x00FF0000, 0x0000FF00,
											   0x000000FF, 0xFF000000);
	if (!shadow)
		return;
	SDL_FillRect(shadow, NULL, SDL_MapRGBA(shadow->format, 0, 0, 0, alpha));

	SDL_Rect dst_rect = {
		rect->x - offset + 2, rect->y - offset + 2,
		shadow->w, shadow->h};
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

	// Fill with dark background as fallback
	SDL_FillRect(canvas, NULL, SDL_MapRGBA(canvas->format, 20, 20, 20, 255));

	int padding = 12;

	// Layer 1: Screenshot as background (fill)
	if (screenshot_path) {
		SDL_Surface* ss = IMG_Load(screenshot_path);
		if (ss) {
			SDL_Surface* bg = scaleSurfaceToFill(ss, CANVAS_W, CANVAS_H);
			if (bg) {
				SDL_SetSurfaceBlendMode(bg, SDL_BLENDMODE_NONE);
				SDL_BlitSurface(bg, NULL, canvas, NULL);
				SDL_FreeSurface(bg);
			}
			SDL_FreeSurface(ss);
		}
	}

	// Layer 2: Box art (bottom-left, ~35% canvas height)
	if (boxart_path) {
		SDL_Surface* box = IMG_Load(boxart_path);
		if (box) {
			int max_h = (int)(CANVAS_H * 0.45);
			int max_w = (int)(CANVAS_W * 0.35);
			SDL_Surface* scaled = scaleSurface(box, max_w, max_h);
			if (scaled) {
				SDL_Rect box_rect = {
					padding,
					CANVAS_H - scaled->h - padding,
					scaled->w, scaled->h};
				// Draw shadow
				drawShadow(canvas, &box_rect, 3, 100);
				// Blit box art
				SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_BLEND);
				SDL_BlitSurface(scaled, NULL, canvas, &box_rect);
				SDL_FreeSurface(scaled);
			}
			SDL_FreeSurface(box);
		}
	}

	// Layer 3: Wheel/logo (bottom-right, ~30% canvas width)
	if (wheel_path) {
		SDL_Surface* wheel = IMG_Load(wheel_path);
		if (wheel) {
			int max_w = (int)(CANVAS_W * 0.45);
			int max_h = (int)(CANVAS_H * 0.25);
			SDL_Surface* scaled = scaleSurface(wheel, max_w, max_h);
			if (scaled) {
				SDL_Rect wheel_rect = {
					CANVAS_W - scaled->w - padding,
					CANVAS_H - scaled->h - padding,
					scaled->w, scaled->h};
				// Draw shadow
				drawShadow(canvas, &wheel_rect, 2, 80);
				// Blit wheel
				SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_BLEND);
				SDL_BlitSurface(scaled, NULL, canvas, &wheel_rect);
				SDL_FreeSurface(scaled);
			}
			SDL_FreeSurface(wheel);
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
