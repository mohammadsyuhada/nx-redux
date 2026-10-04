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
#include "placeholder_art.h"
#include "area_scale.h"

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
	   KIND_BACKDROP,
	   KIND_BOXPH }; // the placeholder case's abstract plate (box-art pool)

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
static unsigned lastGen = 0; // gen of the slot the latest lookup returned (UI thread only)
static SDL_mutex* mutex = NULL;
static SDL_cond* cond = NULL;
static SDL_Thread* worker = NULL;
static SDL_atomic_t shutdownFlag;
static SDL_atomic_t loadedFlag;

///////////////////////////////////////
// Picture shaping (worker thread)

// The slot being built and its gen at pick time (worker thread only). Between the stages the build checks whether the
// UI thread evicted or forgot the slot meanwhile (art scrolled past) and stops early; the hand-back's gen check
// still drops a result that loses the race after the last check.
static const Slot* buildSlot = NULL;
static unsigned buildGen = 0;

static bool cancelled(void) {
	if (!buildSlot)
		return false;
	SDL_LockMutex(mutex);
	bool gone = !buildSlot->used || buildSlot->gen != buildGen;
	SDL_UnlockMutex(mutex);
	return gone;
}

// Frees s and returns true when the build was cancelled.
static bool dropIfCancelled(SDL_Surface* s) {
	if (!cancelled())
		return false;
	if (s)
		SDL_FreeSurface(s);
	return true;
}

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
			// two channels a word (0x00RR00BB, 0x00AA00GG): a lane's sum of four <= 1022 never carries over
			Uint32 rb = (a & 0x00FF00FFu) + (b & 0x00FF00FFu) + (c & 0x00FF00FFu) + (d & 0x00FF00FFu) + 0x00020002u;
			Uint32 ag = ((a >> 8) & 0x00FF00FFu) + ((b >> 8) & 0x00FF00FFu) + ((c >> 8) & 0x00FF00FFu) +
						((d >> 8) & 0x00FF00FFu) + 0x00020002u;
			dp[y * dpitch + x] = ((rb >> 2) & 0x00FF00FFu) | (((ag >> 2) & 0x00FF00FFu) << 8);
		}
	}
	*keep = (HomeArtRect){0, 0, w, h};
	return dst;
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
	int spitch = src->pitch / 4;
	const Uint32* sp = (const Uint32*)src->pixels + keep.y * spitch + keep.x;
	int ok = AreaScale_bilinearCover(sp, keep.w, keep.h, spitch, dst->pixels, w, h, dst->pitch / 4, s, ox, oy);
	SDL_FreeSurface(src);
	if (ok != 0) {
		SDL_FreeSurface(dst);
		return NULL;
	}
	return dst;
}

static SDL_Surface* screenshotFor(const char* rom, HomeArtRect* keep) {
	char path[MAX_PATH];
	ROM_findScreenshot(rom, path, sizeof(path)); // the scraped screenshot, else the root .media picture (Ports)
	SDL_Surface* s = loadArgb(path);
	if (s)
		*keep = (HomeArtRect){0, 0, s->w, s->h};
	return s;
}

// A game without a screenshot (the Carousel's and Grid's tiles) or box art (the Backdrop's placeholder case): its
// abstract picture (placeholder_art.c, seeded by
// the ROM's file name), generated once at PLACEHOLDER_W×H on this worker (~50 ms on the Smart Pro S) and kept as a
// PNG under PLACEHOLDER_DIR, so a later view only loads it. Not in the game's .media: the Artwork Manager still sees
// the game as missing art, and a real screenshot, once fetched, is found first.
#define PLACEHOLDER_DIR SHARED_USERDATA_PATH "/.minui/placeholders"
#define PLACEHOLDER_W 640 // a tile's (landscape)
#define PLACEHOLDER_H 360
#define PLACEHOLDER_BOX_W 360 // a box art's (the placeholder case's 0.72 portrait), saved as <hash>_box.png
#define PLACEHOLDER_BOX_H 500

// The PNG write (~100+ ms on the card) waits in a small queue so it never delays a decode: the worker writes one only
// when no request is queued, and the rest when it stops. Worker thread only.
#define PENDING_PNG_MAX 8
typedef struct {
	char path[MAX_PATH];
	SDL_Surface* surface;
} PendingPng;
static PendingPng pendingPngs[PENDING_PNG_MAX];
static int pendingPngCount = 0;

// written whole, then renamed in: a half-written file is never loaded
static void writePlaceholderPng(SDL_Surface* s, const char* path) {
	char tmp[MAX_PATH + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir_p(PLACEHOLDER_DIR);
	if (IMG_SavePNG(s, tmp) == 0)
		rename(tmp, path);
	else
		unlink(tmp);
}

// Write the oldest queued placeholder PNG; false when none is queued.
static bool writeOnePendingPng(void) {
	if (pendingPngCount == 0)
		return false;
	PendingPng p = pendingPngs[0];
	memmove(pendingPngs, pendingPngs + 1, sizeof(PendingPng) * (size_t)(pendingPngCount - 1));
	pendingPngCount--;
	writePlaceholderPng(p.surface, p.path);
	SDL_FreeSurface(p.surface);
	return true;
}

// Queue a copy of s for writing to path (the caller keeps s); a full queue writes its oldest first.
static void queuePlaceholderPng(SDL_Surface* s, const char* path) {
	SDL_Surface* copy = SDL_DuplicateSurface(s);
	if (!copy) {
		writePlaceholderPng(s, path);
		return;
	}
	if (pendingPngCount == PENDING_PNG_MAX)
		writeOnePendingPng();
	PendingPng* p = &pendingPngs[pendingPngCount++];
	snprintf(p->path, sizeof(p->path), "%s", path);
	p->surface = copy;
}

static SDL_Surface* placeholderFor(const char* rom, bool box, HomeArtRect* keep) {
	const char* base = strrchr(rom, '/');
	base = base ? base + 1 : rom;
	char seed[MAX_PATH];
	snprintf(seed, sizeof(seed), "%s", base);
	char* dot = strrchr(seed, '.');
	if (dot && dot != seed)
		*dot = '\0';
	uint32_t hash = 2166136261u;
	for (const char* c = seed; *c; c++)
		hash = (hash ^ (uint8_t)*c) * 16777619u;
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/%08x%s.png", PLACEHOLDER_DIR, hash, box ? "_box" : "");
	SDL_Surface* s = NULL;
	for (int i = 0; i < pendingPngCount && !s; i++) // generated a moment ago, not on the card yet
		if (!strcmp(pendingPngs[i].path, path))
			s = SDL_DuplicateSurface(pendingPngs[i].surface);
	if (!s)
		s = loadArgb(path);
	if (!s) {
		s = SDL_CreateRGBSurfaceWithFormat(0, box ? PLACEHOLDER_BOX_W : PLACEHOLDER_W, box ? PLACEHOLDER_BOX_H : PLACEHOLDER_H,
										   32, SDL_PIXELFORMAT_ARGB8888);
		if (!s)
			return NULL;
		PlaceholderArt_render(s->pixels, s->w, s->h, s->pitch / 4, seed);
		queuePlaceholderPng(s, path);
	}
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
	if (!src || dropIfCancelled(src))
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
	if (!art || dropIfCancelled(art))
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
	if (!src || dropIfCancelled(src))
		return NULL;
	SDL_Surface* out = cropFill(src, keep, w, h, 1.0f, HomeArt_frameY(keep.w, keep.h, 0.5f));
	if (!out || dropIfCancelled(out))
		return NULL;
	Uint32* dp = out->pixels;
	int dpitch = out->pitch / 4;
	for (int y = 0; y < h; y++) {
		int k = (int)(Row_backdropGain(h > 1 ? (float)y / (h - 1) : 0.0f) * 256.0f + 0.5f); // once per row
		Uint32* row = dp + y * dpitch;
		// (c * k + 128) >> 8 two channels a word (0x00RR00BB, then G): k <= 256 keeps a lane under 65536
		for (int x = 0; x < w; x++) {
			Uint32 px = row[x];
			Uint32 rb = ((px & 0x00FF00FFu) * k + 0x00800080u) >> 8;
			Uint32 g = ((px & 0x0000FF00u) * k + 0x00008000u) >> 8;
			row[x] = 0xFF000000u | (rb & 0x00FF00FFu) | (g & 0x0000FF00u);
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
	if (kind == KIND_BOXPH) {
		HomeArtRect keep = {0};
		SDL_Surface* src = placeholderFor(rom, true, &keep);
		return src && !dropIfCancelled(src) ? cropFill(src, keep, w, h, 1.0f, 0.5f) : NULL;
	}
	HomeArtRect keep = {0};
	SDL_Surface* src = NULL;
	if (kind == KIND_CONTINUE) {
		src = loadArgb(preview);
		if (src) {
			SDL_LockSurface(src);
			keep = HomeArt_trimLetterbox(src->pixels, src->w, src->h, src->pitch / 4);
			bool blank = HomeArt_isBlankFrame(src->pixels, keep, src->pitch / 4);
			SDL_UnlockSurface(src);
			if (blank) { // a black resume frame shows nothing: the screenshot instead (else the abstract picture)
				SDL_FreeSurface(src);
				src = NULL;
			}
		}
	}
	if (!src)
		src = screenshotFor(rom, &keep);
	bool abstract = !src; // no screenshot either: the game's abstract picture, for a pin and for Continue alike
	if (abstract)
		src = placeholderFor(rom, false, &keep);
	if (!src || dropIfCancelled(src))
		return NULL;
	bool cont = kind == KIND_CONTINUE && !abstract; // the resume frame's zoom and framing are for a game's own picture
	SDL_Surface* out = cropFill(src, keep, w, h, cont ? CONTINUE_ZOOM : 1.0f,
								HomeArt_frameY(keep.w, keep.h, cont ? CONTINUE_FRAME_Y : 0.5f));
	if (out && radius > 0)
		GFX_ApplyRoundedCorners_8888(out, NULL, radius);
	return out;
}

///////////////////////////////////////
// Worker

static int workerMain(void* arg) {
	(void)arg;
	PWR_pinHelperThread(); // the app's helper core set, when one is recorded (no-op otherwise)
	SDL_LockMutex(mutex);
	while (!SDL_AtomicGet(&shutdownFlag)) {
		// newest request first: it is what the screen shows now
		Slot* pick = NULL;
		for (int i = 0; i < HOMEART_CACHE_SIZE; i++)
			if (slots[i].used && slots[i].queued && (!pick || slots[i].lru > pick->lru))
				pick = &slots[i];
		if (!pick) {
			if (pendingPngCount > 0) { // idle: one queued placeholder PNG, then look for requests again
				SDL_UnlockMutex(mutex);
				writeOnePendingPng();
				SDL_LockMutex(mutex);
				continue;
			}
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
		buildSlot = pick;
		buildGen = gen;
		SDL_Surface* result = buildPicture(kind, rom, preview, w, h, radius, &ox, &oy);
		buildSlot = NULL;

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
	while (writeOnePendingPng()) // stopping: the placeholders not on the card yet
		;
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
	if (kind == KIND_BOXART || kind == KIND_BOXPH) {
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
	lastGen = 0;
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
	lastGen = hit->gen;
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

HomeArtState HomeArt_boxPlaceholder(const char* rom_path, int w, int h, SDL_Surface** out) {
	return lookup(KIND_BOXPH, rom_path, NULL, w, h, 0, out, NULL, NULL);
}

HomeArtState HomeArt_backdrop(const char* rom_path, int screen_w, int screen_h, SDL_Surface** out) {
	return lookup(KIND_BACKDROP, rom_path, NULL, screen_w, screen_h, 0, out, NULL, NULL);
}

unsigned HomeArt_lastGen(void) {
	return lastGen;
}

bool HomeArt_checkAsyncLoaded(void) {
	return SDL_AtomicCAS(&loadedFlag, 1, 0);
}

void HomeArt_forget(const char* rom_path) {
	if (!rom_path || !mutex)
		return;
	SDL_LockMutex(mutex);
	for (int i = 0; i < HOMEART_CACHE_SIZE; i++) {
		Slot* s = &slots[i];
		if (!s->used || strcmp(s->rom, rom_path) != 0)
			continue;
		if (s->surface)
			SDL_FreeSurface(s->surface);
		s->surface = NULL;
		s->used = false; // a result the worker is still building is dropped (its slot no longer matches)
		s->gen = ++genCounter;
	}
	SDL_UnlockMutex(mutex);
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
	return Recents_firstRom();
}
