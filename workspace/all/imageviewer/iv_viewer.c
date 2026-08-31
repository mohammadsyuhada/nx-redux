#include <math.h>
#include <stdio.h>
#include <string.h>

#include "iv_viewer.h"
#include "api.h"
#include "iv_defines.h"
#include "iv_probe.h"
#include "ui_buttonhintbar.h"
#include "ui_draw.h"
#include "ui_loadingoverlay.h"
#include "ui_toast.h"

// Overlay only appears once a decode is actually slow (~100ms+), so a fast
// cache-adjacent load never flashes it for a frame or two.
#define IV_LOADING_OVERLAY_DELAY_MS 100

// Fullscreen image viewer: fit/zoom/pan over the current image, prev/next
// navigation with wraparound, a small next/prev prefetch cache, and a
// toggleable info overlay. See iv_viewer.h for the public contract.

static ImageBrowserContext* vw_browser;
static int vw_index;
static SDL_Surface* vw_image; // owned, screen format
static int vw_ow, vw_oh;	  // decode dimensions of vw_image
static int vw_zoom_idx;		  // 0 = fit; 1..3 index into vw_levels above fit
static float vw_fit;		  // fit scale, recomputed per image
static int vw_cx, vw_cy;	  // pan center, image coords
static bool vw_info;
static uint32_t vw_hint_until;	 // hint bar auto-hides after this SDL_GetTicks()
static bool vw_hint_was_visible; // hint-bar visibility as of the previous update, to catch its expiry edge
static bool vw_loading;
static uint32_t vw_load_start; // tick when the in-flight FULL request was issued; gates the loading overlay
static IvLoadStatus vw_last_err;
static char vw_toast[64];
static uint32_t vw_toast_time;
static const float vw_levels[] = {1.0f, 2.0f, 4.0f};

// Next/prev prefetch sequencing: vw_prefetch_step 0 = next not yet
// settled, 1 = next settled/skipped and prev not yet settled, 2 = both
// done. Only one IV_LOAD_PREFETCH request is ever outstanding at a time
// (the loader's pending slot is latest-wins per purpose), so the two
// targets are requested one after the other rather than both at once.
static int vw_prefetch_step;
static char vw_prefetch_next_path[512];
static char vw_prefetch_prev_path[512];

// --- Prefetch cache: 3-slot LRU, bounded by a total-pixel budget ------------

#define IV_CACHE_SLOTS 3
#define IV_CACHE_PIXEL_BUDGET (IV_MAX_PIXELS + 8 * 1000 * 1000)

typedef struct {
	char path[512];
	SDL_Surface* surf;
	int ow, oh;
	uint32_t lru;
	bool used;
} IvCacheSlot;

static IvCacheSlot vw_cache[IV_CACHE_SLOTS];

static bool cache_contains(const char* path) {
	for (int i = 0; i < IV_CACHE_SLOTS; i++)
		if (vw_cache[i].used && strcmp(vw_cache[i].path, path) == 0)
			return true;
	return false;
}

// Removes and returns a hit; ownership of the surface moves to the caller.
static bool cache_take(const char* path, SDL_Surface** out_surf, int* out_ow, int* out_oh) {
	for (int i = 0; i < IV_CACHE_SLOTS; i++) {
		if (vw_cache[i].used && strcmp(vw_cache[i].path, path) == 0) {
			*out_surf = vw_cache[i].surf;
			*out_ow = vw_cache[i].ow;
			*out_oh = vw_cache[i].oh;
			vw_cache[i].used = false;
			vw_cache[i].surf = NULL;
			return true;
		}
	}
	return false;
}

static long cache_total_pixels(void) {
	long total = 0;
	for (int i = 0; i < IV_CACHE_SLOTS; i++)
		if (vw_cache[i].used)
			total += (long)vw_cache[i].ow * vw_cache[i].oh;
	return total;
}

// Inserts with LRU eviction; frees `surf` instead of caching it if the pixel
// budget can't be met even after evicting the least-recently-used entry.
static void cache_put(const char* path, SDL_Surface* surf, int ow, int oh) {
	for (int i = 0; i < IV_CACHE_SLOTS; i++) {
		if (vw_cache[i].used && strcmp(vw_cache[i].path, path) == 0) {
			SDL_FreeSurface(vw_cache[i].surf);
			vw_cache[i].surf = surf;
			vw_cache[i].ow = ow;
			vw_cache[i].oh = oh;
			vw_cache[i].lru = SDL_GetTicks();
			return;
		}
	}

	int slot = -1;
	for (int i = 0; i < IV_CACHE_SLOTS; i++) {
		if (!vw_cache[i].used) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		slot = 0;
		for (int i = 1; i < IV_CACHE_SLOTS; i++)
			if (vw_cache[i].lru < vw_cache[slot].lru)
				slot = i;
		SDL_FreeSurface(vw_cache[slot].surf);
		vw_cache[slot].used = false;
	}

	if (cache_total_pixels() + (long)ow * oh > IV_CACHE_PIXEL_BUDGET) {
		SDL_FreeSurface(surf);
		return;
	}

	vw_cache[slot].used = true;
	snprintf(vw_cache[slot].path, sizeof(vw_cache[slot].path), "%s", path);
	vw_cache[slot].surf = surf;
	vw_cache[slot].ow = ow;
	vw_cache[slot].oh = oh;
	vw_cache[slot].lru = SDL_GetTicks();
}

static void cache_clear(void) {
	for (int i = 0; i < IV_CACHE_SLOTS; i++) {
		if (vw_cache[i].used) {
			SDL_FreeSurface(vw_cache[i].surf);
			vw_cache[i].surf = NULL;
			vw_cache[i].used = false;
		}
	}
}

// --- Navigation --------------------------------------------------------

static void vw_reset_to_fit(void) {
	vw_zoom_idx = 0;
	vw_cx = vw_image->w / 2;
	vw_cy = vw_image->h / 2;
}

// Nearest non-dir entry in `dir` (+1/-1) from vw_index, wrapping around. In
// a folder with a single file (optionally alongside subdirs), this lands
// back on vw_index itself.
static int vw_step_index(int dir) {
	int n = vw_browser->entry_count;
	if (n <= 0)
		return vw_index;
	int i = vw_index;
	for (int steps = 0; steps < n; steps++) {
		i = (i + dir + n) % n;
		if (!vw_browser->entries[i].is_dir)
			return i;
	}
	return vw_index;
}

// Advance the next/prev prefetch sequence: skip a target that's empty,
// already cached, or is the image currently being viewed (single-image
// folder), otherwise request it and wait for that result before starting
// the other one.
static void vw_prefetch_advance(void) {
	while (vw_prefetch_step < 2) {
		const char* target = vw_prefetch_step == 0 ? vw_prefetch_next_path : vw_prefetch_prev_path;
		const char* current = vw_browser->entries[vw_index].path;
		if (target[0] == '\0' || strcmp(target, current) == 0 || cache_contains(target)) {
			vw_prefetch_step++;
			continue;
		}
		// OOM hardening: prefetching is speculative, so skip a neighbor the
		// header probe confirms is oversized rather than decoding it on the
		// chance it's never viewed (see IV_PREFETCH_MAX_PIXELS). A failed
		// probe still prefetches - the loader's own IV_MAX_PIXELS backstop
		// (post-decode, in decodeRequest) still applies either way.
		int probe_w = 0, probe_h = 0;
		if (IvProbe_dimensions(target, &probe_w, &probe_h) &&
			(long)probe_w * probe_h > IV_PREFETCH_MAX_PIXELS) {
			vw_prefetch_step++;
			continue;
		}
		IvLoader_request(target, IV_LOAD_PREFETCH);
		return;
	}
}

static void vw_start_prefetch(void) {
	snprintf(vw_prefetch_next_path, sizeof(vw_prefetch_next_path), "%s",
			 vw_browser->entries[vw_step_index(1)].path);
	snprintf(vw_prefetch_prev_path, sizeof(vw_prefetch_prev_path), "%s",
			 vw_browser->entries[vw_step_index(-1)].path);
	vw_prefetch_step = 0;
	vw_prefetch_advance();
}

// Adopt a newly-available image (from a cache hit or a finished FULL load):
// takes ownership of `s`, resets zoom/pan to fit-centered, and (re)starts
// the next/prev prefetch sequence for the new position.
static void vw_apply_image(SDL_Surface* s, int ow, int oh) {
	if (vw_image)
		SDL_FreeSurface(vw_image);
	vw_image = s;
	vw_ow = ow;
	vw_oh = oh;

	SDL_Surface* screen = GFX_getScreen();
	vw_fit = fminf((float)screen->w / s->w, (float)screen->h / s->h);
	vw_reset_to_fit();

	vw_start_prefetch();
}

// Start loading `index`: a cache hit applies synchronously, a miss issues a
// FULL request and leaves vw_loading set until the poll drain sees it land.
static void vw_load_index(int index) {
	vw_index = index;
	vw_loading = true;
	const char* path = vw_browser->entries[index].path;
	SDL_Surface* hit;
	int ow, oh;
	if (cache_take(path, &hit, &ow, &oh)) {
		vw_loading = false;
		vw_apply_image(hit, ow, oh);
	} else {
		vw_load_start = SDL_GetTicks();
		IvLoader_request(path, IV_LOAD_FULL);
	}
}

static void vw_nav(int dir) {
	vw_hint_until = SDL_GetTicks() + 3000;
	vw_load_index(vw_step_index(dir));
}

// --- Zoom/pan geometry ---------------------------------------------------

// Clamped source rect for the current pan/zoom state at `scale` (zoomed
// only): centered on vw_cx/vw_cy, sized to the visible image area, kept
// inside the image bounds. Shared by input-time pan clamping and render.
static SDL_Rect vw_src_rect(SDL_Surface* screen, float scale) {
	int vis_w = (int)(screen->w / scale), vis_h = (int)(screen->h / scale);
	SDL_Rect src = {vw_cx - vis_w / 2, vw_cy - vis_h / 2,
					vis_w > vw_image->w ? vw_image->w : vis_w,
					vis_h > vw_image->h ? vw_image->h : vis_h};
	if (src.x < 0)
		src.x = 0;
	if (src.y < 0)
		src.y = 0;
	if (src.x + src.w > vw_image->w)
		src.x = vw_image->w - src.w;
	if (src.y + src.h > vw_image->h)
		src.y = vw_image->h - src.h;
	return src;
}

static void vw_clamp_pan(SDL_Surface* screen, float scale) {
	SDL_Rect src = vw_src_rect(screen, scale);
	vw_cx = src.x + src.w / 2;
	vw_cy = src.y + src.h / 2;
}

// Keeps `message`'s GPU toast layer in sync with its lifetime: forces a
// redraw every frame it's visible, then once more on the expiry edge
// (clearing the message) so the next render call actually wipes the layer -
// otherwise UI_renderToast only re-clears when called again, and nothing
// would call it once the viewer goes idle. Mirrors ModuleCommon_tickToast in
// workspace/all/mediaplayer/module_common.c.
static void tick_toast(char* message, uint32_t toast_time, bool* dirty) {
	if (message[0] == '\0')
		return;
	if (SDL_GetTicks() - toast_time < TOAST_DURATION) {
		*dirty = true;
	} else {
		message[0] = '\0';
		*dirty = true;
	}
}

// --- Public API ----------------------------------------------------------

void IvViewer_open(ImageBrowserContext* browser, int entry_index) {
	vw_browser = browser;
	vw_image = NULL;
	vw_ow = vw_oh = 0;
	vw_zoom_idx = 0;
	vw_fit = 1.0f;
	vw_cx = vw_cy = 0;
	vw_info = false;
	vw_hint_until = SDL_GetTicks() + 3000;
	vw_hint_was_visible = true;
	vw_last_err = IV_LOAD_OK;
	vw_toast[0] = '\0';
	vw_toast_time = 0;
	vw_prefetch_step = 2; // nothing to prefetch until an image actually applies

	vw_load_index(entry_index);
}

IvViewerStatus IvViewer_update(SDL_Surface* screen, bool* dirty) {
	IvLoadResult res;
	while (IvLoader_poll(&res)) {
		if (res.purpose == IV_LOAD_PREFETCH) {
			if (res.status == IV_LOAD_OK && res.surface)
				cache_put(res.path, res.surface, res.orig_w, res.orig_h);
			else if (res.surface)
				SDL_FreeSurface(res.surface);
			if (vw_prefetch_step < 2) {
				vw_prefetch_step++;
				vw_prefetch_advance();
			}
			continue;
		}
		if (res.purpose != IV_LOAD_FULL) {
			// PREVIEW belongs to the browser's own drain; if one lands
			// while we're the active screen, leave it be (it can't - the
			// browser only issues PREVIEW requests while it's active -
			// but stay defensive rather than leak).
			if (res.surface)
				SDL_FreeSurface(res.surface);
			continue;
		}
		if (strcmp(res.path, vw_browser->entries[vw_index].path) != 0) {
			// Stale: superseded by navigation before this result arrived.
			if (res.surface)
				SDL_FreeSurface(res.surface);
			continue;
		}
		vw_loading = false;
		if (res.status == IV_LOAD_OK) {
			vw_apply_image(res.surface, res.orig_w, res.orig_h);
			*dirty = true;
		} else {
			vw_last_err = res.status;
			if (!vw_image)
				return IV_VIEWER_ERROR; // initial open failed
			snprintf(vw_toast, sizeof(vw_toast), "%s",
					 res.status == IV_LOAD_ERR_TOO_LARGE ? "Image too large" : "Couldn't load image");
			vw_toast_time = SDL_GetTicks();
			*dirty = true;
		}
	}

	// Toast tick, same idiom as the browser's: keeps redrawing while
	// vw_toast is visible, then once more on the expiry edge so the final
	// render call wipes the GPU layer.
	tick_toast(vw_toast, vw_toast_time, dirty);

	// Hint-bar expiry edge: IvViewer_busy() below stops treating the viewer
	// as busy the instant SDL_GetTicks() passes vw_hint_until, so without
	// this the last rendered frame (drawn just before expiry, hint bar
	// still visible) is simply never replaced - nothing else forces a
	// redraw once the loop goes idle. Firing *dirty here, exactly once on
	// the visible->hidden transition, gets that one last frame rendered.
	bool hint_visible = SDL_GetTicks() < vw_hint_until;
	if (vw_hint_was_visible && !hint_visible)
		*dirty = true;
	vw_hint_was_visible = hint_visible;

	if (vw_loading) {
		if (PAD_justPressed(BTN_B))
			return IV_VIEWER_EXIT; // cancel back to browser
		return IV_VIEWER_CONTINUE;
	}

	float scale = vw_zoom_idx == 0 ? vw_fit : vw_levels[vw_zoom_idx - 1];
	bool zoomed = vw_zoom_idx > 0;

	if (PAD_justPressed(BTN_R1)) {
		int next_idx = 0;
		for (int lvl = 0; lvl < 3; lvl++) {
			if (vw_levels[lvl] > scale + 0.01f) {
				next_idx = lvl + 1;
				break;
			}
		}
		if (next_idx > 0) {
			vw_zoom_idx = next_idx;
			*dirty = true;
		}
	}
	if (PAD_justPressed(BTN_L1) && zoomed) {
		int first_above_fit = 4; // sentinel: no preset level exceeds fit
		for (int lvl = 0; lvl < 3; lvl++) {
			if (vw_levels[lvl] > vw_fit + 0.01f) {
				first_above_fit = lvl + 1;
				break;
			}
		}
		if (vw_zoom_idx <= first_above_fit)
			vw_reset_to_fit();
		else
			vw_zoom_idx--;
		*dirty = true;
	}

	if (zoomed) {
		int step = (int)(SCALE1(24) / scale);
		if (step < 1)
			step = 1;
		bool panned = false;
		if (PAD_justRepeated(BTN_LEFT)) {
			vw_cx -= step;
			panned = true;
		}
		if (PAD_justRepeated(BTN_RIGHT)) {
			vw_cx += step;
			panned = true;
		}
		if (PAD_justRepeated(BTN_UP)) {
			vw_cy -= step;
			panned = true;
		}
		if (PAD_justRepeated(BTN_DOWN)) {
			vw_cy += step;
			panned = true;
		}
		if (panned) {
			vw_clamp_pan(screen, scale);
			*dirty = true;
		}
	} else {
		if (PAD_justRepeated(BTN_LEFT)) {
			vw_nav(-1);
			*dirty = true;
		}
		if (PAD_justRepeated(BTN_RIGHT)) {
			vw_nav(1);
			*dirty = true;
		}
	}

	if (PAD_justPressed(BTN_A)) {
		vw_info = !vw_info;
		vw_hint_until = SDL_GetTicks() + 3000;
		*dirty = true;
	}

	if (PAD_justPressed(BTN_B)) {
		if (zoomed) {
			vw_reset_to_fit();
			*dirty = true;
		} else {
			return IV_VIEWER_EXIT;
		}
	}

	return IV_VIEWER_CONTINUE;
}

static void vw_render_info(SDL_Surface* screen, float scale) {
	char name_buf[256];
	ImageBrowser_getDisplayName(vw_browser->entries[vw_index].name, name_buf, sizeof(name_buf));
	char dims_buf[64];
	snprintf(dims_buf, sizeof(dims_buf), "%dx%d \xc2\xb7 %d%%", vw_ow, vw_oh, (int)(scale * 100));

	int pos = 0, total = 0;
	for (int i = 0; i < vw_browser->entry_count; i++) {
		if (vw_browser->entries[i].is_dir)
			continue;
		total++;
		if (i == vw_index)
			pos = total;
	}
	char pos_buf[32];
	snprintf(pos_buf, sizeof(pos_buf), "%d / %d", pos, total);

	SDL_Surface* lines[3] = {
		GFX_renderText(font.small, name_buf, COLOR_WHITE),
		GFX_renderText(font.small, dims_buf, COLOR_WHITE),
		GFX_renderText(font.small, pos_buf, COLOR_WHITE),
	};

	int pad = SCALE1(8);
	int max_w = 0;
	for (int i = 0; i < 3; i++)
		if (lines[i] && lines[i]->w > max_w)
			max_w = lines[i]->w;
	int line_h = TTF_FontHeight(font.small);
	int panel_w = max_w + pad * 2;
	int panel_h = line_h * 3 + pad * 2;
	int panel_x = SCALE1(10), panel_y = SCALE1(10);

	// Own small ARGB surface so the panel can be semi-transparent (screen
	// has no usable alpha channel); rounded via UI_fillRoundedRect, same
	// technique as ui_toast.c's panel, blended onto screen on blit.
	static SDL_Surface* panel = NULL;
	if (!panel || panel->w != panel_w || panel->h != panel_h) {
		if (panel)
			SDL_FreeSurface(panel);
		panel = SDL_CreateRGBSurface(SDL_SWSURFACE, panel_w, panel_h, 32,
									 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
		if (panel) {
			UI_fillRoundedRect(panel, 0, 0, panel_w, panel_h, SCALE1(8),
							   SDL_MapRGBA(panel->format, 0, 0, 0, 178));
			SDL_SetSurfaceBlendMode(panel, SDL_BLENDMODE_BLEND);
		}
	}
	if (panel)
		SDL_BlitSurface(panel, NULL, screen, &(SDL_Rect){panel_x, panel_y});

	int ty = panel_y + pad;
	for (int i = 0; i < 3; i++) {
		if (!lines[i])
			continue;
		SDL_BlitSurface(lines[i], NULL, screen, &(SDL_Rect){panel_x + pad, ty});
		ty += line_h;
		SDL_FreeSurface(lines[i]);
	}
}

void IvViewer_render(SDL_Surface* screen) {
	GFX_clear(screen);
	float scale = vw_zoom_idx == 0 ? vw_fit : vw_levels[vw_zoom_idx - 1];
	if (vw_image) {
		if (vw_zoom_idx == 0) {
			GFX_blitScaleAspect(vw_image, screen);
		} else {
			SDL_Rect src = vw_src_rect(screen, scale);
			SDL_Rect dst = {(screen->w - (int)(src.w * scale)) / 2,
							(screen->h - (int)(src.h * scale)) / 2,
							(int)(src.w * scale), (int)(src.h * scale)};
			SDL_BlitScaled(vw_image, &src, screen, &dst);
		}
	}
	// Only show the overlay once the decode has actually taken a while;
	// gated on wall-clock time from when the FULL request was issued so a
	// fast (cache-adjacent) load never flashes it for a frame or two.
	if (vw_loading && SDL_GetTicks() - vw_load_start > IV_LOADING_OVERLAY_DELAY_MS)
		UI_renderLoadingOverlay(screen, "Loading image...", NULL);
	if (vw_info && vw_image)
		vw_render_info(screen, scale);
	if (SDL_GetTicks() < vw_hint_until || vw_info) {
		static char* hints[] = {"B", "BACK", "A", "INFO", NULL};
		UI_renderButtonHintBar(screen, hints);
	}
	// Unconditional: UI_renderToast clears its GPU layer itself when the
	// message is empty or expired, which is what actually makes the toast
	// disappear once tick_toast() clears the message on the expiry edge.
	UI_renderToast(screen, vw_toast, vw_toast_time);
}

IvLoadStatus IvViewer_lastError(void) {
	return vw_last_err;
}

bool IvViewer_busy(void) {
	return vw_loading || vw_prefetch_step < 2 ||
		   (vw_toast[0] && SDL_GetTicks() - vw_toast_time < TOAST_DURATION) ||
		   SDL_GetTicks() < vw_hint_until;
}

void IvViewer_close(void) {
	if (vw_image) {
		SDL_FreeSurface(vw_image);
		vw_image = NULL;
	}
	cache_clear();
	vw_browser = NULL;
	vw_toast[0] = '\0';
	vw_prefetch_step = 2;
}
