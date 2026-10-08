#include "scraper_compositor.h"
#include "../nextui/area_scale.h"
#include "api.h" // LOG_warn
#include "png_palette.h"
#include "scraper_core.h" // Scraper_variantPath
#include "utils.h"
#include <stdio.h>
#include <string.h>

// Compositor_createSingle bound (the screenshot and 3D box art variants)
#define SINGLE_W 640
#define SINGLE_H 480
// The mix canvas: the List's fitted box width, 4:3 (scraper_compositor.h)
#define MIX_W COMPOSITOR_MIX_W
#define MIX_H COMPOSITOR_MIX_H
// Its pixel constants, the 1024x768 originals x0.375 (rounded, at least 1): the box art and wheel inset from the
// canvas edges (19 -> 7) and their drop shadows' offsets (5 -> 2, 3 -> 1), still a visible step at this size.
#define MIX_PADDING 7
#define MIX_BOX_SHADOW 2
#define MIX_WHEEL_SHADOW 1

static SDL_Surface* newARGB(int w, int h) {
	return SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
}

// Nearest-neighbour blow-up of an ARGB8888 surface by a whole factor: every
// source pixel becomes a k x k block.
static SDL_Surface* scaleNearestInt(SDL_Surface* src, int k) {
	SDL_Surface* dst = newARGB(src->w * k, src->h * k);
	if (!dst)
		return NULL;
	const int spitch = src->pitch / 4, dpitch = dst->pitch / 4;
	for (int y = 0; y < dst->h; y++) {
		const Uint32* srow = (const Uint32*)src->pixels + (y / k) * spitch;
		Uint32* drow = (Uint32*)dst->pixels + y * dpitch;
		for (int x = 0; x < dst->w; x++)
			drow[x] = srow[x / k];
	}
	return dst;
}

// Resize an ARGB8888 surface to exactly w x h for the mix, keeping it sharp:
// - shrinking uses the area filter (AreaScale_argb, shared with nextui): every
//   output pixel averages all the source pixels it covers, with alpha-weighted
//   colour, instead of SDL_BlitScaled's nearest pick that drops detail and
//   stair-steps logo edges;
// - growing first repeats pixels by the largest whole factor that still fits
//   (nearest), so pixel art (a 160x144 Game Boy screen) stays hard-edged
//   blocks, then the area filter does the last < 2x step, which only blends
//   the one output pixel straddling each source-pixel boundary.
static SDL_Surface* resizeSharp(SDL_Surface* src, int w, int h) {
	SDL_Surface* from = src;
	int k = (w / src->w < h / src->h) ? w / src->w : h / src->h;
	if (k >= 2) {
		from = scaleNearestInt(src, k);
		if (!from)
			return NULL;
	}
	SDL_Surface* dst = newARGB(w, h);
	if (dst && AreaScale_argb((const uint32_t*)from->pixels, from->w, from->h, from->pitch / 4,
							  (uint32_t*)dst->pixels, w, h, dst->pitch / 4) != 0) {
		SDL_FreeSurface(dst);
		dst = NULL;
	}
	if (from != src)
		SDL_FreeSurface(from);
	return dst;
}

// The size that fits src_w x src_h within max_w x max_h, aspect kept (at
// least 1x1); `grow` false caps the scale at 1.
static void fitSize(int src_w, int src_h, int max_w, int max_h, bool grow, int* w, int* h) {
	double scale_x = (double)max_w / src_w;
	double scale_y = (double)max_h / src_h;
	double scale = (scale_x < scale_y) ? scale_x : scale_y;
	if (!grow && scale > 1.0)
		scale = 1.0;
	*w = (int)(src_w * scale);
	*h = (int)(src_h * scale);
	if (*w < 1)
		*w = 1;
	if (*h < 1)
		*h = 1;
}

// Load an image as ARGB8888 (paletted/8-bit PNGs otherwise lose alpha when
// scaled). NULL on failure.
static SDL_Surface* loadARGB(const char* path) {
	SDL_Surface* raw = IMG_Load(path);
	if (!raw)
		return NULL;
	SDL_Surface* img = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	return img;
}

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

// Scale an ARGB8888 surface to fill target_w x target_h (aspect kept, the
// overflow cropped from the centre), sharply (resizeSharp).
static SDL_Surface* fillSharp(SDL_Surface* src, int target_w, int target_h) {
	double scale_x = (double)target_w / src->w;
	double scale_y = (double)target_h / src->h;
	double scale = (scale_x > scale_y) ? scale_x : scale_y;
	int scaled_w = (int)(src->w * scale);
	int scaled_h = (int)(src->h * scale);
	if (scaled_w < target_w)
		scaled_w = target_w;
	if (scaled_h < target_h)
		scaled_h = target_h;

	SDL_Surface* scaled = resizeSharp(src, scaled_w, scaled_h);
	if (!scaled)
		return NULL;
	SDL_Surface* dst = newARGB(target_w, target_h);
	if (!dst) {
		SDL_FreeSurface(scaled);
		return NULL;
	}
	SDL_Rect src_rect = {(scaled_w - target_w) / 2, (scaled_h - target_h) / 2, target_w, target_h};
	SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(scaled, &src_rect, dst, NULL);
	SDL_FreeSurface(scaled);
	return dst;
}

// Load `path` and fit it sharply within max_w x max_h (grown if smaller).
static SDL_Surface* loadFitSharp(const char* path, int max_w, int max_h) {
	SDL_Surface* img = loadARGB(path);
	if (!img)
		return NULL;
	int w, h;
	fitSize(img->w, img->h, max_w, max_h, true, &w, &h);
	SDL_Surface* scaled = resizeSharp(img, w, h);
	SDL_FreeSurface(img);
	return scaled;
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

	SDL_Surface* canvas = newARGB(MIX_W, MIX_H);
	if (!canvas)
		return NULL;

	// Transparent background
	SDL_FillRect(canvas, NULL, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));

	int padding = MIX_PADDING;
	// Screenshot inset so box art and wheel float outside its edges
	int ss_pad_x = (int)(MIX_W * 0.06);		 // left/right padding
	int ss_pad_bottom = (int)(MIX_H * 0.12); // bottom padding
	int ss_area_w = MIX_W - ss_pad_x * 2;
	int ss_area_h = MIX_H - ss_pad_bottom;

	// Layer 1: Screenshot (inset with padding, top-aligned)
	if (screenshot_path) {
		SDL_Surface* ss = loadARGB(screenshot_path);
		if (ss) {
			SDL_Surface* bg = fillSharp(ss, ss_area_w, ss_area_h);
			if (bg) {
				SDL_Rect dst_rect = {ss_pad_x, 0, bg->w, bg->h};
				SDL_SetSurfaceBlendMode(bg, SDL_BLENDMODE_NONE);
				SDL_BlitSurface(bg, NULL, canvas, &dst_rect);
				SDL_FreeSurface(bg);
			}
			SDL_FreeSurface(ss);
		}
	}

	// Layer 2: Box art (bottom-left, half canvas size)
	if (boxart_path) {
		SDL_Surface* scaled = loadFitSharp(boxart_path, (int)(MIX_W * 0.50), (int)(MIX_H * 0.50));
		if (scaled) {
			SDL_Rect box_rect = {padding, MIX_H - scaled->h - padding, scaled->w, scaled->h};
			drawShapeShadow(canvas, scaled, &box_rect, MIX_BOX_SHADOW, 100);
			SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_BLEND);
			SDL_BlitSurface(scaled, NULL, canvas, &box_rect);
			SDL_FreeSurface(scaled);
		}
	}

	// Layer 3: Wheel/logo (bottom-right, half canvas width)
	if (wheel_path) {
		SDL_Surface* scaled = loadFitSharp(wheel_path, (int)(MIX_W * 0.50), (int)(MIX_H * 0.30));
		if (scaled) {
			SDL_Rect wheel_rect = {MIX_W - scaled->w - padding, MIX_H - scaled->h - padding, scaled->w, scaled->h};
			drawShapeShadow(canvas, scaled, &wheel_rect, MIX_WHEEL_SHADOW, 80);
			SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_BLEND);
			SDL_BlitSurface(scaled, NULL, canvas, &wheel_rect);
			SDL_FreeSurface(scaled);
		}
	}

	return canvas;
}

bool Compositor_saveMix(const char* screenshot_path, const char* boxart_path,
						const char* wheel_path, const char* out_png) {
	SDL_Surface* mix = Compositor_create(screenshot_path, boxart_path, wheel_path);
	if (!mix)
		return false;
	char mix_path[MAX_PATH];
	Scraper_variantPath(out_png, "mix", mix_path, sizeof(mix_path));
	bool saved = Compositor_savePNGRGBA(mix, mix_path);
	SDL_FreeSurface(mix);
	return saved;
}

SDL_Surface* Compositor_createSingle(const char* image_path) {
	if (!image_path)
		return NULL;

	SDL_Surface* raw = IMG_Load(image_path);
	if (!raw)
		return NULL;

	// Convert to ARGB32 so scaling and PNG saving behave the same for every
	// source (paletted PNGs otherwise lose alpha).
	SDL_Surface* img = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!img)
		return NULL;

	// Fit within the 4:3 bound, keep aspect; the canvas is exactly the scaled
	// size — no padding, shadow, or transparent bars.
	SDL_Surface* canvas = scaleSurface(img, SINGLE_W, SINGLE_H);
	SDL_FreeSurface(img);
	return canvas;
}

SDL_Surface* Compositor_createUpTo(const char* image_path, int max_w, int max_h) {
	if (!image_path)
		return NULL;
	SDL_Surface* img = loadARGB(image_path);
	if (!img)
		return NULL;
	int w, h;
	fitSize(img->w, img->h, max_w, max_h, false, &w, &h);
	if (w == img->w && h == img->h)
		return img; // already fits: keep every source pixel
	SDL_Surface* scaled = resizeSharp(img, w, h);
	SDL_FreeSurface(img);
	return scaled;
}

// Quantize an ARGB8888 surface (converting other formats first) and write it
// as an indexed PNG via png_palette (tmp file + rename).
static bool savePalettePNG(SDL_Surface* surface, const char* path) {
	SDL_Surface* argb = surface;
	if (surface->format->format != SDL_PIXELFORMAT_ARGB8888) {
		argb = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
		if (!argb)
			return false;
	}
	bool ok = false;
	if (SDL_LockSurface(argb) == 0) {
		ok = PngPalette_saveARGB((const uint32_t*)argb->pixels, argb->w, argb->h, argb->pitch / 4, path);
		SDL_UnlockSurface(argb);
	}
	if (argb != surface)
		SDL_FreeSurface(argb);
	return ok;
}

// Create the directory `path` sits in, if missing.
static void makeParentDir(const char* path) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	char* last_slash = strrchr(dir, '/');
	if (last_slash) {
		*last_slash = '\0';
		mkdir_p(dir);
	}
}

bool Compositor_savePNG(SDL_Surface* surface, const char* path) {
	if (!surface || !path)
		return false;

	makeParentDir(path);

	// Game art goes to the card as a 256-colour PNG: same size on screen, a
	// third of the bytes or less. Any failure there (an odd surface format,
	// out of memory, a write error) falls back to the plain 32-bit PNG.
	if (savePalettePNG(surface, path))
		return true;
	LOG_warn("Scraper: 256-colour save failed, writing a full-colour PNG: %s\n", path);

	SDL_RWops* rw = SDL_RWFromFile(path, "wb");
	if (!rw)
		return false;

	int ret = IMG_SavePNG_RW(surface, rw, 1);
	return ret == 0;
}

bool Compositor_savePNGRGBA(SDL_Surface* surface, const char* path) {
	if (!surface || !path)
		return false;
	makeParentDir(path);

	// ARGB8888 has an alpha mask, so IMG_SavePNG writes 8-bit RGBA, no palette.
	SDL_Surface* argb = surface;
	if (surface->format->format != SDL_PIXELFORMAT_ARGB8888) {
		argb = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
		if (!argb)
			return false;
	}
	// Written beside and renamed over, so a reader never sees half a file.
	char tmp[MAX_PATH];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	bool ok = IMG_SavePNG(argb, tmp) == 0 && rename(tmp, path) == 0;
	if (!ok)
		remove(tmp);
	if (argb != surface)
		SDL_FreeSurface(argb);
	return ok;
}
