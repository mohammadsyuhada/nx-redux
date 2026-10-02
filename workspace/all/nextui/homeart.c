#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "defines.h"
#include "api.h"
#include "config.h"
#include "utils.h"
#include "recents.h"
#include "homeart.h"
#include "homeart_model.h"
#include "row_model.h"

// One worker thread decodes and shapes pictures into a small cache. The UI thread only looks slots up, queues misses
// and evicts; the worker fills a queued slot under the mutex, and a generation number drops a result whose slot was
// evicted (and reused) while it was being decoded. READY surfaces are never touched by the worker again, so a
// pointer handed out stays valid until the UI thread itself evicts that slot.
//
// The slots are split into pools that evict independently, so a burst of one kind never pushes out another: 24 for
// Continue/pin pictures, 24 for box arts, 3 for the full-screen Backdrop pictures (~3.6 MB each at 1280x720).

#define POOL_PICTURE_SIZE 24
#define POOL_BOXART_SIZE 24
#define POOL_BACKDROP_SIZE 3
#define HOMEART_CACHE_SIZE (POOL_PICTURE_SIZE + POOL_BOXART_SIZE + POOL_BACKDROP_SIZE)
#define CONTINUE_ZOOM 1.15f
#define CONTINUE_FRAME_Y 0.4f // the crop window sits 40% of the way down the overflow

enum { KIND_CONTINUE,
	   KIND_PIN,
	   KIND_BOXART,
	   KIND_BACKDROP };

#define SHADOW_ALPHA 0.5f // the shadow is the art's alpha at 50%
#define SHADOW_OFFSET_DP 4
#define SHADOW_BLUR_DP 8 // ~ Gaussian extent, as 3 box passes of radius blur/3
#define SHADOW_PASSES 3

typedef struct {
	bool used;
	int kind;
	char rom[MAX_PATH];
	char preview[MAX_PATH]; // Continue only
	int w, h, radius;
	HomeArtState state;
	bool queued; // waiting for the worker
	SDL_Surface* surface;
	int ox, oy; // box art: the art's top-left inside the padded surface
	unsigned lru;
	unsigned gen;
} Slot;

static Slot slots[HOMEART_CACHE_SIZE];
static unsigned lruCounter = 0;
static unsigned genCounter = 0;
static SDL_mutex* mutex = NULL;
static SDL_cond* cond = NULL;
static SDL_Thread* worker = NULL;
static SDL_atomic_t shutdownFlag;
static SDL_atomic_t loadedFlag;

///////////////////////////////////////
// Picture shaping (worker thread)

static SDL_Surface* loadArgb(const char* path) {
	if (!path || !path[0] || access(path, F_OK) != 0)
		return NULL;
	SDL_Surface* img = IMG_Load(path); // the resume preview is PNG data despite its .bmp name
	if (!img)
		return NULL;
	SDL_Surface* argb = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(img);
	return argb;
}

// 2x2 box-filter halving of `keep` into a new surface, so the bilinear pass never skips source pixels.
static SDL_Surface* halve(SDL_Surface* src, HomeArtRect* keep) {
	int w = keep->w / 2, h = keep->h / 2;
	SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!dst)
		return NULL;
	const Uint32* sp = src->pixels;
	int spitch = src->pitch / 4;
	Uint32* dp = dst->pixels;
	int dpitch = dst->pitch / 4;
	for (int y = 0; y < h; y++) {
		const Uint32* r0 = sp + (keep->y + 2 * y) * spitch + keep->x;
		const Uint32* r1 = r0 + spitch;
		for (int x = 0; x < w; x++) {
			Uint32 a = r0[2 * x], b = r0[2 * x + 1], c = r1[2 * x], d = r1[2 * x + 1];
			Uint32 out = 0;
			for (int sh = 0; sh < 32; sh += 8) {
				unsigned sum = ((a >> sh) & 0xFF) + ((b >> sh) & 0xFF) + ((c >> sh) & 0xFF) + ((d >> sh) & 0xFF);
				out |= ((sum + 2) / 4) << sh;
			}
			dp[y * dpitch + x] = out;
		}
	}
	*keep = (HomeArtRect){0, 0, w, h};
	return dst;
}

static Uint32 lerpPixel(Uint32 a, Uint32 b, int t) { // t in 0..256
	Uint32 out = 0;
	for (int sh = 0; sh < 32; sh += 8) {
		int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
		out |= (Uint32)((ca * (256 - t) + cb * t + 128) >> 8) << sh;
	}
	return out;
}

// Scale `keep` of `src` (ARGB8888) to cover w×h, times `zoom`, and cut the w×h window: centred across, `frame_y` of
// the way down the vertical overflow. With frame_y = 0.4 the source's 40% line lands on the window's 40% line at any
// scale, so the zoom happens about that line. Takes ownership of `src`.
static SDL_Surface* cropFill(SDL_Surface* src, HomeArtRect keep, int w, int h, float zoom, float frame_y) {
	if (!src || keep.w <= 0 || keep.h <= 0) {
		if (src)
			SDL_FreeSurface(src);
		return NULL;
	}
	float s = fmaxf((float)w / keep.w, (float)h / keep.h) * zoom;
	while (s <= 0.5f && keep.w >= 2 && keep.h >= 2) {
		SDL_Surface* half = halve(src, &keep);
		if (!half)
			break;
		SDL_FreeSurface(src);
		src = half;
		s *= 2;
	}
	SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!dst) {
		SDL_FreeSurface(src);
		return NULL;
	}
	float ox = 0.5f * (keep.w * s - w);
	float oy = frame_y * (keep.h * s - h);
	const Uint32* sp = src->pixels;
	int spitch = src->pitch / 4;
	Uint32* dp = dst->pixels;
	int dpitch = dst->pitch / 4;
	int maxx = keep.w - 1, maxy = keep.h - 1;
	for (int y = 0; y < h; y++) {
		float fy = (y + 0.5f + oy) / s - 0.5f;
		if (fy < 0)
			fy = 0;
		if (fy > maxy)
			fy = maxy;
		int y0 = (int)fy, y1 = y0 < maxy ? y0 + 1 : y0;
		int ty = (int)((fy - y0) * 256);
		const Uint32* r0 = sp + (keep.y + y0) * spitch + keep.x;
		const Uint32* r1 = sp + (keep.y + y1) * spitch + keep.x;
		for (int x = 0; x < w; x++) {
			float fx = (x + 0.5f + ox) / s - 0.5f;
			if (fx < 0)
				fx = 0;
			if (fx > maxx)
				fx = maxx;
			int x0 = (int)fx, x1 = x0 < maxx ? x0 + 1 : x0;
			int tx = (int)((fx - x0) * 256);
			dp[y * dpitch + x] = lerpPixel(lerpPixel(r0[x0], r0[x1], tx), lerpPixel(r1[x0], r1[x1], tx), ty);
		}
	}
	SDL_FreeSurface(src);
	return dst;
}

static SDL_Surface* screenshotFor(const char* rom, HomeArtRect* keep) {
	char path[MAX_PATH];
	ROM_displayArtPath(rom, ART_TYPE_SCREENSHOT, false, path, sizeof(path));
	SDL_Surface* s = loadArgb(path);
	if (s)
		*keep = (HomeArtRect){0, 0, s->w, s->h};
	return s;
}

// Black-with-alpha shadow of `art` under the art itself, in a surface padded by `pad` px on every side (the art's
// top-left sits at (pad, pad)). The shadow is the art's alpha at 50%, `off` px down, box-blurred SHADOW_PASSES times
// with radius r. `pad` >= off + r*passes keeps the blur's support inside the buffer (it treats outside as zero).
static SDL_Surface* addShadow(SDL_Surface* art, int pad, int off, int r) {
	int W = art->w + 2 * pad, H = art->h + 2 * pad;
	SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
	unsigned char* a = calloc((size_t)W * H, 1);
	unsigned char* tmp = malloc((size_t)W * H);
	if (!dst || !a || !tmp) {
		if (dst)
			SDL_FreeSurface(dst);
		free(a);
		free(tmp);
		return NULL;
	}
	const Uint32* sp = art->pixels;
	int spitch = art->pitch / 4;
	for (int y = 0; y < art->h; y++) {
		unsigned char* row = a + (size_t)(y + pad + off) * W + pad;
		for (int x = 0; x < art->w; x++)
			row[x] = (unsigned char)(((sp[y * spitch + x] >> 24) * (int)(SHADOW_ALPHA * 256) + 128) >> 8);
	}
	Row_boxBlurAlpha(a, tmp, W, H, r, SHADOW_PASSES);
	free(tmp);

	// art over black-with-alpha (non-premultiplied): out_a = aa + sa(1-aa), out_rgb = rgb*aa/out_a
	Uint32* dp = dst->pixels;
	int dpitch = dst->pitch / 4;
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			unsigned sa = a[(size_t)y * W + x];
			int ax = x - pad, ay = y - pad;
			Uint32 px = (ax >= 0 && ay >= 0 && ax < art->w && ay < art->h) ? sp[ay * spitch + ax] : 0;
			unsigned aa = px >> 24;
			if (aa == 255) {
				dp[y * dpitch + x] = px;
				continue;
			}
			unsigned oa = aa + (sa * (255 - aa) + 127) / 255;
			if (oa == 0) {
				dp[y * dpitch + x] = 0;
				continue;
			}
			Uint32 out = (Uint32)oa << 24;
			for (int sh = 0; sh < 24; sh += 8)
				out |= (((px >> sh) & 0xFF) * aa + oa / 2) / oa << sh;
			dp[y * dpitch + x] = out;
		}
	}
	free(a);
	return dst;
}

// Box art fitted (contain) into w×h with its soft shadow; *ox/*oy = the art's top-left in the result.
static SDL_Surface* buildBoxart(const char* rom, int w, int h, int* ox, int* oy) {
	char path[MAX_PATH];
	ROM_displayArtPath(rom, ART_TYPE_BOXART, false, path, sizeof(path));
	SDL_Surface* src = loadArgb(path);
	if (!src)
		return NULL;
	float s = fminf((float)w / src->w, (float)h / src->h);
	int dw = (int)(src->w * s + 0.5f), dh = (int)(src->h * s + 0.5f);
	if (dw < 1)
		dw = 1;
	if (dh < 1)
		dh = 1;
	if (dw > w)
		dw = w;
	if (dh > h)
		dh = h;
	// same aspect as the source, so the cover crop of cropFill cuts at most a rounding pixel
	SDL_Surface* art = cropFill(src, (HomeArtRect){0, 0, src->w, src->h}, dw, dh, 1.0f, 0.5f);
	if (!art)
		return NULL;

	int off = NX_DP(SHADOW_OFFSET_DP);
	int r = NX_DP(SHADOW_BLUR_DP) / 3;
	if (r < 1)
		r = 1;
	int pad = off + r * SHADOW_PASSES + 1;
	Uint32 t0 = SDL_GetTicks();
	SDL_Surface* out = addShadow(art, pad, off, r);
	Uint32 ms = SDL_GetTicks() - t0;
	static bool logged = false; // the worker is the only caller
	if (out && !logged) {
		logged = true;
		LOG_debug("boxart shadow: %d ms (%dx%d)\n", (int)ms, out->w, out->h);
	}
	SDL_FreeSurface(art);
	if (!out)
		return NULL;
	*ox = *oy = pad;
	return out;
}

// The Backdrop picture: the screenshot cropped to fill w×h (centred), dimmed 65% (ROW_BACKDROP_DIM), then each row
// darkened by the shade curve (Row_backdropGain). Opaque.
static SDL_Surface* buildBackdrop(const char* rom, int w, int h) {
	HomeArtRect keep = {0};
	SDL_Surface* src = screenshotFor(rom, &keep);
	if (!src)
		return NULL;
	SDL_Surface* out = cropFill(src, keep, w, h, 1.0f, 0.5f);
	if (!out)
		return NULL;
	Uint32* dp = out->pixels;
	int dpitch = out->pitch / 4;
	for (int y = 0; y < h; y++) {
		int k = (int)(Row_backdropGain(h > 1 ? (float)y / (h - 1) : 0.0f) * 256.0f + 0.5f); // once per row
		Uint32* row = dp + y * dpitch;
		for (int x = 0; x < w; x++) {
			Uint32 px = row[x];
			Uint32 o = 0xFF000000u;
			for (int sh = 0; sh < 24; sh += 8)
				o |= (Uint32)((((px >> sh) & 0xFF) * k + 128) >> 8) << sh;
			row[x] = o;
		}
	}
	SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_NONE);
	return out;
}

static SDL_Surface* buildPicture(int kind, const char* rom, const char* preview, int w, int h, int radius, int* ox,
								 int* oy) {
	if (kind == KIND_BOXART)
		return buildBoxart(rom, w, h, ox, oy);
	if (kind == KIND_BACKDROP)
		return buildBackdrop(rom, w, h);
	HomeArtRect keep = {0};
	SDL_Surface* src = NULL;
	if (kind == KIND_CONTINUE) {
		src = loadArgb(preview);
		if (src) {
			SDL_LockSurface(src);
			keep = HomeArt_trimLetterbox(src->pixels, src->w, src->h, src->pitch / 4);
			bool blank = HomeArt_isBlankFrame(src->pixels, keep, src->pitch / 4);
			SDL_UnlockSurface(src);
			if (blank) { // a black resume frame shows nothing: the screenshot instead (else the title card)
				SDL_FreeSurface(src);
				src = NULL;
			}
		}
	}
	if (!src)
		src = screenshotFor(rom, &keep);
	if (!src)
		return NULL;
	bool cont = kind == KIND_CONTINUE;
	SDL_Surface* out = cropFill(src, keep, w, h, cont ? CONTINUE_ZOOM : 1.0f, cont ? CONTINUE_FRAME_Y : 0.5f);
	if (out && radius > 0)
		GFX_ApplyRoundedCorners_8888(out, NULL, radius);
	return out;
}

///////////////////////////////////////
// Worker

static int workerMain(void* arg) {
	(void)arg;
	SDL_LockMutex(mutex);
	while (!SDL_AtomicGet(&shutdownFlag)) {
		// newest request first: it is what the screen shows now
		Slot* pick = NULL;
		for (int i = 0; i < HOMEART_CACHE_SIZE; i++)
			if (slots[i].used && slots[i].queued && (!pick || slots[i].lru > pick->lru))
				pick = &slots[i];
		if (!pick) {
			SDL_CondWait(cond, mutex);
			continue;
		}
		pick->queued = false;
		int kind = pick->kind, w = pick->w, h = pick->h, radius = pick->radius;
		unsigned gen = pick->gen;
		char rom[MAX_PATH], preview[MAX_PATH];
		memcpy(rom, pick->rom, sizeof(rom));
		memcpy(preview, pick->preview, sizeof(preview));
		SDL_UnlockMutex(mutex);

		int ox = 0, oy = 0;
		SDL_Surface* result = buildPicture(kind, rom, preview, w, h, radius, &ox, &oy);

		SDL_LockMutex(mutex);
		if (pick->used && pick->gen == gen) {
			pick->surface = result;
			pick->ox = ox;
			pick->oy = oy;
			pick->state = result ? HOMEART_READY : HOMEART_NONE;
			SDL_AtomicSet(&loadedFlag, 1);
		} else if (result) {
			SDL_FreeSurface(result); // evicted while decoding
		}
	}
	SDL_UnlockMutex(mutex);
	return 0;
}

static bool ensureStarted(void) {
	if (worker)
		return true;
	if (!mutex)
		mutex = SDL_CreateMutex();
	if (!cond)
		cond = SDL_CreateCond();
	if (!mutex || !cond)
		return false;
	SDL_AtomicSet(&shutdownFlag, 0);
	worker = SDL_CreateThread(workerMain, "HomeArtWorker", NULL);
	if (!worker)
		fprintf(stderr, "homeart: failed to create worker thread\n");
	return worker != NULL;
}

///////////////////////////////////////
// Cache (UI thread)

// Each kind evicts within its own pool: [first, first + count).
static void poolFor(int kind, int* first, int* count) {
	if (kind == KIND_BOXART) {
		*first = POOL_PICTURE_SIZE;
		*count = POOL_BOXART_SIZE;
	} else if (kind == KIND_BACKDROP) {
		*first = POOL_PICTURE_SIZE + POOL_BOXART_SIZE;
		*count = POOL_BACKDROP_SIZE;
	} else {
		*first = 0;
		*count = POOL_PICTURE_SIZE;
	}
}

static HomeArtState lookup(int kind, const char* rom, const char* preview, int w, int h, int radius,
						   SDL_Surface** out, int* ox, int* oy) {
	if (out)
		*out = NULL;
	if (ox)
		*ox = 0;
	if (oy)
		*oy = 0;
	if (!rom || !rom[0] || w <= 0 || h <= 0 || !ensureStarted())
		return HOMEART_NONE;
	if (!preview)
		preview = "";

	SDL_LockMutex(mutex);
	Slot* hit = NULL;
	Slot* victim = NULL;
	int first, count;
	poolFor(kind, &first, &count);
	for (int i = first; i < first + count; i++) {
		Slot* s = &slots[i];
		if (!s->used) {
			if (!victim || victim->used)
				victim = s;
			continue;
		}
		if (s->kind == kind && s->w == w && s->h == h && s->radius == radius && !strcmp(s->rom, rom) &&
			!strcmp(s->preview, preview)) {
			hit = s;
			break;
		}
		if (!victim || (victim->used && s->lru < victim->lru))
			victim = s;
	}
	if (!hit) {
		hit = victim;
		if (hit->surface)
			SDL_FreeSurface(hit->surface);
		*hit = (Slot){.used = true, .kind = kind, .w = w, .h = h, .radius = radius, .state = HOMEART_LOADING, .queued = true, .gen = ++genCounter};
		snprintf(hit->rom, sizeof(hit->rom), "%s", rom);
		snprintf(hit->preview, sizeof(hit->preview), "%s", preview);
		SDL_CondSignal(cond);
	}
	hit->lru = ++lruCounter;
	HomeArtState state = hit->state;
	if (state == HOMEART_READY) {
		if (out)
			*out = hit->surface;
		if (ox)
			*ox = hit->ox;
		if (oy)
			*oy = hit->oy;
	}
	SDL_UnlockMutex(mutex);
	return state;
}

HomeArtState HomeArt_continue(const char* rom_path, const char* preview_path, int w, int h, int radius_px,
							  SDL_Surface** out) {
	return lookup(KIND_CONTINUE, rom_path, preview_path, w, h, radius_px, out, NULL, NULL);
}

HomeArtState HomeArt_pin(const char* rom_path, int w, int h, int radius_px, SDL_Surface** out) {
	return lookup(KIND_PIN, rom_path, NULL, w, h, radius_px, out, NULL, NULL);
}

HomeArtState HomeArt_boxart(const char* rom_path, int w, int h, SDL_Surface** out, int* ox, int* oy) {
	return lookup(KIND_BOXART, rom_path, NULL, w, h, 0, out, ox, oy);
}

HomeArtState HomeArt_backdrop(const char* rom_path, int screen_w, int screen_h, SDL_Surface** out) {
	return lookup(KIND_BACKDROP, rom_path, NULL, screen_w, screen_h, 0, out, NULL, NULL);
}

bool HomeArt_checkAsyncLoaded(void) {
	return SDL_AtomicCAS(&loadedFlag, 1, 0);
}

void HomeArt_quit(void) {
	if (worker) {
		SDL_LockMutex(mutex);
		SDL_AtomicSet(&shutdownFlag, 1);
		SDL_CondSignal(cond);
		SDL_UnlockMutex(mutex);
		SDL_WaitThread(worker, NULL);
		worker = NULL;
	}
	for (int i = 0; i < HOMEART_CACHE_SIZE; i++) {
		if (slots[i].surface)
			SDL_FreeSurface(slots[i].surface);
		slots[i] = (Slot){0};
	}
	lruCounter = 0;
	if (cond) {
		SDL_DestroyCond(cond);
		cond = NULL;
	}
	if (mutex) {
		SDL_DestroyMutex(mutex);
		mutex = NULL;
	}
	SDL_AtomicSet(&loadedFlag, 0);
}

///////////////////////////////////////
// Continue entry

Entry* Home_continueEntry(void) {
	// Recents_getEntries() hands back fresh Entries in a fresh Array; keep the first ROM, free the rest.
	Array* entries = Recents_getEntries();
	Entry* found = NULL;
	for (int i = 0; i < entries->count; i++) {
		Entry* e = entries->items[i];
		if (!found && e->type == ENTRY_ROM) {
			found = e;
			entries->items[i] = NULL;
		}
	}
	EntryArray_free(entries); // Entry_free skips the NULL left where `found` was
	return found;
}
