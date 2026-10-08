// The tab-focus dim as one layer: snapshot the area under the content, let the content draw, then mix it back toward
// the snapshot at the dim's opacity (see contentdim.h).
//
// The screen surface is premultiplied (SDL's surface blend onto the transparent screen leaves rgb·a, alpha a, and
// generic_video.c composites it over the page background with a premultiplied blend). In premultiplied terms the
// content as a layer at opacity k over what was under it is u + k·(s − u) on every channel, alpha included: the
// straight lerp UI_blitOpaque does (NEON on the device). A pixel the content left alone keeps its value; an opaque
// content pixel over a clear one becomes k of itself, which the GPU then shows over the page background at k, so the
// background shows through at 1 − k (true opacity, not a black wash).

#include "contentdim.h"

#include "api.h"

#include "menutabs.h"
#include "ui_fade.h"

static SDL_Surface* snap = NULL; // the area before the content (screen-sized, only the area is used)
static SDL_Rect snap_area;
static int snap_alpha = 255;
static bool layered = false;

static int dimAlpha255(void) {
	return (int)(MenuTabs_contentAlpha() * 255.0f + 0.5f);
}

bool ContentDim_dimmed(void) {
	return dimAlpha255() < 255;
}

void ContentDim_begin(SDL_Surface* screen, SDL_Rect area) {
	layered = false;
	if (!screen || screen->format->format != SDL_PIXELFORMAT_ARGB8888)
		return;
	int a = dimAlpha255();
	if (a >= 255) {
		if (snap) { // settled back to lit: the snapshot isn't needed until the next focus
			SDL_FreeSurface(snap);
			snap = NULL;
		}
		return; // lit: draw straight on, nothing to undo
	}
	SDL_Rect c;
	if (!SDL_IntersectRect(&area, &(SDL_Rect){0, 0, screen->w, screen->h}, &c))
		return;
	if (!snap || snap->w != screen->w || snap->h != screen->h) {
		if (snap)
			SDL_FreeSurface(snap);
		snap = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!snap)
			return; // no memory: the content draws lit rather than not at all
	}
	// a straight copy (no blend) of the area, at the same place in the screen-sized snapshot (UI_blitOpaque reads the
	// content and the snapshot at the same coordinates)
	SDL_BlendMode bm;
	SDL_GetSurfaceBlendMode(screen, &bm);
	SDL_SetSurfaceBlendMode(screen, SDL_BLENDMODE_NONE);
	SDL_Rect src = c;
	SDL_BlitSurface(screen, &src, snap, &(SDL_Rect){c.x, c.y, c.w, c.h});
	SDL_SetSurfaceBlendMode(screen, bm);
	snap_area = c;
	snap_alpha = a;
	layered = true;
}

void ContentDim_beginClear(SDL_Surface* screen, SDL_Rect area) {
	layered = false;
	int a = dimAlpha255();
	if (!screen || a >= 255) {
		ContentDim_begin(screen, area); // lit: frees the snapshot
		return;
	}
	SDL_Rect c;
	if (!SDL_IntersectRect(&area, &(SDL_Rect){0, 0, screen->w, screen->h}, &c))
		return;
	if (!PLAT_setScreenDim(c.y, c.h, (Uint8)a))
		ContentDim_begin(screen, area);
}

void ContentDim_end(SDL_Surface* screen) {
	if (!layered)
		return;
	layered = false;
	if (!screen || !snap)
		return;
	// in place: each pixel of the area becomes snapshot + alpha · (content − snapshot)
	SDL_Rect prev_clip;
	SDL_GetClipRect(screen, &prev_clip);
	SDL_SetClipRect(screen, &snap_area);
	UI_blitOpaque(screen, snap, snap_area.x, snap_area.y, snap_area.w, snap_area.h, screen, snap_area.x, snap_area.y,
				  snap_alpha);
	SDL_SetClipRect(screen, &prev_clip);
}

bool ContentDim_layered(void) {
	return layered;
}

void ContentDim_quit(void) {
	if (snap)
		SDL_FreeSurface(snap);
	snap = NULL;
	layered = false;
}
