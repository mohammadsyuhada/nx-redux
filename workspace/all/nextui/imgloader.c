#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include "defines.h"
#include "api.h"
#include "config.h"
#include "imgloader.h"
#include "artbg.h"
#include "area_scale.h"
#include "homeart.h"
#include "ui_image.h"

///////////////////////////////////////
// Internal task queue structures

typedef struct {
	char imagePath[MAX_PATH];
	BackgroundLoadedCallback callback;
	int compose; // thumbnails: THUMB_COMPOSE_* (the background loader leaves it 0)
} LoadBackgroundTask;

typedef struct TaskNode {
	LoadBackgroundTask* task;
	struct TaskNode* next;
} TaskNode;

///////////////////////////////////////
// Generic task queue

typedef struct TaskQueue {
	TaskNode* head;
	TaskNode* tail;
	int size;
	SDL_mutex* mutex;
	SDL_cond* cond;
} TaskQueue;

///////////////////////////////////////
// Internal state

static TaskQueue bgQueue = {0};
static TaskQueue thumbQueue = {0};

static SDL_Thread* bgLoadThread = NULL;
static SDL_Thread* thumbLoadThread = NULL;

static SDL_atomic_t workerThreadsShutdown; // Flag to signal threads to exit (atomic for thread safety)

static SDL_atomic_t needDrawAtomic;

// Cached screen properties (set once in initImageLoaderPool, safe to read from worker threads)
static Uint32 cachedScreenFormat = 0;
static int cachedScreenW = 0;
// The fitted art box's right margin: the List's right padding, SCALE1(BUTTON_MARGIN) (ArtBg_fitRect)
static int cachedFitMargin = 0;
static int cachedScreenH = 0;

///////////////////////////////////////
// Thumbnail cache
//
// Keyed by path and compose mode (thumbKey): the screenshot's faded background and the fitted box (List art = Mix,
// 2D box art or Wheel) are different surfaces of what may be the same file (a Port's root picture is both its
// screenshot and its legacy mix), so a Layouts > List art switch never reuses the other mode's surface.

#define THUMB_CACHE_SIZE 8
// "fit:" + a path startLoadThumb accepts (shorter than MAX_PATH, the task's buffer): every key fits whole
#define THUMB_KEY_MAX (MAX_PATH + 8)

typedef struct {
	char path[THUMB_KEY_MAX]; // thumbKey
	SDL_Surface* surface;	  // NULL: a remembered miss (no file, or it failed to decode), not retried until invalidated
	int lru_counter;
	bool occupied;
} ThumbCacheEntry;

static ThumbCacheEntry thumb_cache[THUMB_CACHE_SIZE];
static int thumb_lru_counter = 0;
static char desiredThumbPath[THUMB_KEY_MAX] = {0}; // thumbKey of the art the List asks for
static SDL_atomic_t thumbAsyncLoaded;
// What the thumbnail worker has to do (thumbMutex): the key it is decoding ("" when idle) and the key last queued
// for it, not yet taken ("" when none). startLoadThumb asks for neither again, so a List held on art that is still
// loading (or A -> B -> A) never decodes the same picture twice. thumbCacheInvalidate marks the decode in flight
// stale when it is the invalidated picture: it may have read the old file, so its result is dropped.
static char inflightThumbKey[THUMB_KEY_MAX] = {0};
static bool inflightThumbStale = false;
static char pendingThumbKey[THUMB_KEY_MAX] = {0};

// The cache key for `path` composed as `compose`: the path itself for the background, "fit:" + path for the box.
static void thumbKey(const char* path, int compose, char* out, size_t size) {
	snprintf(out, size, "%s%s", compose == THUMB_COMPOSE_FIT && path[0] ? "fit:" : "", path);
}

///////////////////////////////////////
// Shared state (non-static, externed in imgloader.h)

SDL_mutex* bgMutex = NULL;
SDL_mutex* thumbMutex = NULL;

SDL_Surface* folderbgbmp = NULL;
SDL_Surface* thumbbmp = NULL;
// Bumped (under thumbMutex) whenever thumbbmp is replaced or dropped: the art on the GPU belongs to one generation.
static unsigned thumb_gen = 0;
// thumbbmp is a fitted box (THUMB_COMPOSE_FIT), placed at ArtBg_fitRect rather than ArtBg_originX (thumbMutex)
static bool thumb_fit = false;

// Atomics: the UI thread polls them without the locks (backgroundLayerPending); writers still hold their mutex
// where the change goes with the surfaces it guards (bgMutex: folderbgbmp, thumbMutex: thumbbmp).
static SDL_atomic_t folderbgchanged;
static SDL_atomic_t thumbchanged;

///////////////////////////////////////
// Atomic state accessors

void setNeedDraw(int v) {
	SDL_AtomicSet(&needDrawAtomic, v);
}
int getNeedDraw(void) {
	return SDL_AtomicGet(&needDrawAtomic);
}

///////////////////////////////////////
// Queue management

#define MAX_QUEUE_SIZE 1

// False when the task was dropped (out of memory; it is freed either way).
static bool enqueueTask(TaskQueue* q, LoadBackgroundTask* task) {
	SDL_LockMutex(q->mutex);
	TaskNode* node = (TaskNode*)malloc(sizeof(TaskNode));
	if (!node) {
		free(task);
		SDL_UnlockMutex(q->mutex);
		return false;
	}
	node->task = task;
	node->next = NULL;

	// If queue is full, drop the oldest task (head)
	if (q->size >= MAX_QUEUE_SIZE) {
		TaskNode* oldNode = q->head;
		if (oldNode) {
			q->head = oldNode->next;
			if (!q->head) {
				q->tail = NULL;
			}
			if (oldNode->task) {
				free(oldNode->task);
			}
			free(oldNode);
			q->size--;
		}
	}

	// Enqueue the new task
	if (q->tail) {
		q->tail->next = node;
		q->tail = node;
	} else {
		q->head = q->tail = node;
	}

	q->size++;
	SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);
	return true;
}

// Drop every task still waiting in q (not the one a worker already took).
static void purgeQueue(TaskQueue* q) {
	SDL_LockMutex(q->mutex);
	while (q->head) {
		TaskNode* n = q->head;
		q->head = n->next;
		free(n->task);
		free(n);
	}
	q->tail = NULL;
	q->size = 0;
	SDL_UnlockMutex(q->mutex);
}

///////////////////////////////////////
// Worker thread (shared by BG and Thumb loaders)

// Block until a task is available (or shutdown) and pop it. NULL on shutdown (nothing was dequeued); else the
// caller owns the task.
static LoadBackgroundTask* dequeueTask(TaskQueue* q) {
	SDL_LockMutex(q->mutex);
	while (!q->head && !SDL_AtomicGet(&workerThreadsShutdown)) {
		SDL_CondWait(q->cond, q->mutex);
	}
	if (SDL_AtomicGet(&workerThreadsShutdown)) {
		SDL_UnlockMutex(q->mutex);
		return NULL;
	}
	TaskNode* node = q->head;
	q->head = node->next;
	if (!q->head)
		q->tail = NULL;
	q->size--;
	SDL_UnlockMutex(q->mutex);

	LoadBackgroundTask* task = node->task;
	free(node);
	return task;
}

// Decode the task's image to the screen format: NULL on failure.
static SDL_Surface* decodeTask(const LoadBackgroundTask* task) {
	SDL_Surface* result = NULL;
	// Home's List: a resume frame or an abstract picture by a path of Home's own (homeart.h)
	SDL_Surface* image = HomeArt_isListPath(task->imagePath) ? HomeArt_loadListPath(task->imagePath)
															 : IMG_Load(task->imagePath);
	if (image) {
		result = SDL_ConvertSurfaceFormat(image, cachedScreenFormat, 0);
		SDL_FreeSurface(image);
	}
	return result;
}

static int loadWorker(void* arg) {
	TaskQueue* q = (TaskQueue*)arg;
	while (!SDL_AtomicGet(&workerThreadsShutdown)) {
		LoadBackgroundTask* task = dequeueTask(q);
		if (!task)
			break;
		SDL_Surface* result = decodeTask(task);

		if (task->callback)
			task->callback(result);
		else if (result)
			SDL_FreeSurface(result); // no consumer — don't leak the decode
		free(task);
	}
	return 0;
}

///////////////////////////////////////
// Thumbnail cache helpers (must be called under thumbMutex)
//
// A cached surface is shared, not copied: thumbbmp holds the same surface with its own reference (refcount++), and
// every holder lets go with SDL_FreeSurface. Nothing writes to these pixels (artStep only uploads them), and every
// reference is taken or dropped under thumbMutex (SDL's refcount is not atomic); nextui.c's final free runs after
// the worker is joined.

// Takes over the caller's reference to `surface` (NULL caches a miss).
static void thumbCacheInsert(const char* path, SDL_Surface* surface) {
	int target = -1;
	int min_lru = INT_MAX;

	// Check if already cached (update in place)
	for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
		if (thumb_cache[i].occupied && strcmp(thumb_cache[i].path, path) == 0) {
			SDL_FreeSurface(thumb_cache[i].surface);
			thumb_cache[i].surface = surface;
			thumb_cache[i].lru_counter = ++thumb_lru_counter;
			return;
		}
	}

	// Find empty slot
	for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
		if (!thumb_cache[i].occupied) {
			target = i;
			break;
		}
	}

	// No empty slot - evict LRU
	if (target < 0) {
		for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
			if (thumb_cache[i].lru_counter < min_lru) {
				min_lru = thumb_cache[i].lru_counter;
				target = i;
			}
		}
		if (thumb_cache[target].surface)
			SDL_FreeSurface(thumb_cache[target].surface);
	}

	strncpy(thumb_cache[target].path, path, sizeof(thumb_cache[target].path) - 1);
	thumb_cache[target].path[sizeof(thumb_cache[target].path) - 1] = '\0';
	thumb_cache[target].surface = surface;
	thumb_cache[target].lru_counter = ++thumb_lru_counter;
	thumb_cache[target].occupied = true;
}

// Drop any cached thumbnail (or cached miss) for `path`, in either compose mode, so the next
// startLoadThumb reads it fresh from disk. Safe if not present.
void thumbCacheInvalidate(const char* path) {
	if (!path || !path[0] || strlen(path) >= MAX_PATH)
		return; // startLoadThumb never loads (or caches) a path that long
	char fit_key[THUMB_KEY_MAX];
	thumbKey(path, THUMB_COMPOSE_FIT, fit_key, sizeof(fit_key));
	SDL_LockMutex(thumbMutex);
	for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
		if (thumb_cache[i].occupied &&
			(strcmp(thumb_cache[i].path, path) == 0 || strcmp(thumb_cache[i].path, fit_key) == 0)) {
			if (thumb_cache[i].surface)
				SDL_FreeSurface(thumb_cache[i].surface);
			thumb_cache[i].surface = NULL;
			thumb_cache[i].occupied = false;
		}
	}
	// the List showing it: forget that it asked, so its next startLoadThumb misses the fast path and loads the new
	// file (the old picture stays up until the new one is in)
	if (strcmp(desiredThumbPath, path) == 0 || strcmp(desiredThumbPath, fit_key) == 0)
		desiredThumbPath[0] = '\0';
	// a decode already under way may have read the old file: its result is dropped (a queued one has not read yet)
	if (strcmp(inflightThumbKey, path) == 0 || strcmp(inflightThumbKey, fit_key) == 0)
		inflightThumbStale = true;
	SDL_UnlockMutex(thumbMutex);
}

// Refresh Roms: art may have been copied onto the card since, so every remembered miss is read again. Decoded
// pictures stay (an art change that replaces a file goes through thumbCacheInvalidate).
void thumbCacheForgetMisses(void) {
	SDL_LockMutex(thumbMutex);
	for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
		if (thumb_cache[i].occupied && !thumb_cache[i].surface)
			thumb_cache[i].occupied = false;
	}
	desiredThumbPath[0] = '\0';
	SDL_UnlockMutex(thumbMutex);
}

///////////////////////////////////////
// Dedicated thumbnail worker thread

static int thumbLoadWorker(void* arg) {
	TaskQueue* q = (TaskQueue*)arg;
	while (!SDL_AtomicGet(&workerThreadsShutdown)) {
		LoadBackgroundTask* task = dequeueTask(q);
		if (!task)
			break;
		char key[THUMB_KEY_MAX];
		thumbKey(task->imagePath, task->compose, key, sizeof(key));
		SDL_LockMutex(thumbMutex);
		snprintf(inflightThumbKey, sizeof(inflightThumbKey), "%s", key);
		inflightThumbStale = false;
		if (strcmp(pendingThumbKey, key) == 0)
			pendingThumbKey[0] = '\0'; // taken
		SDL_UnlockMutex(thumbMutex);
		SDL_Surface* result = decodeTask(task);

		if (result) {
			// the List's game art: the screenshot as a right-aligned, full-height surface that fades diagonally into
			// the list, or the mix, 2D box art or wheel fitted into a hard-edged box on the right (Layouts > List
			// art). Both are pure SDL (thread-safe) and leave `result` untouched.
			bool fit = task->compose == THUMB_COMPOSE_FIT;
			SDL_Surface* composed = fit ? ArtBg_composeFit(result, cachedScreenW, cachedScreenH, cachedFitMargin, cachedScreenFormat)
										: ArtBg_compose(result, cachedScreenW, cachedScreenH, cachedScreenFormat);
			SDL_FreeSurface(result);
			// the faded background at half size (area-averaged): it sits faded behind the rows, where the GPU's
			// linear upscale to the screen reads the same, and a quarter of the pixels to upload when the selection
			// brings new art. The fitted box stays at full size: it is solid and sharp, a logo's or a box's edges
			// would blur at half.
			SDL_Surface* half = !fit && composed && composed->format->format == SDL_PIXELFORMAT_ARGB8888
									? SDL_CreateRGBSurfaceWithFormat(0, (composed->w + 1) / 2, (composed->h + 1) / 2, 32,
																	 SDL_PIXELFORMAT_ARGB8888)
									: NULL;
			if (half && AreaScale_argb(composed->pixels, composed->w, composed->h, composed->pitch / 4, half->pixels,
									   half->w, half->h, half->pitch / 4) == 0) {
				SDL_FreeSurface(composed);
				composed = half;
			} else if (half) {
				SDL_FreeSurface(half);
			}
			result = composed; // NULL falls through to the no-thumb path below
		}

		// Cache the result (a miss too, so it isn't decoded again) and conditionally update thumbbmp
		SDL_LockMutex(thumbMutex);
		bool stale = inflightThumbStale;
		inflightThumbKey[0] = '\0';
		inflightThumbStale = false;
		if (stale) {
			// invalidated mid-decode: maybe the old file. Not cached or shown; the List's next ask loads it anew.
			if (result)
				SDL_FreeSurface(result);
			SDL_UnlockMutex(thumbMutex);
			free(task);
			continue;
		}
		bool is_current = (strcmp(key, desiredThumbPath) == 0);
		bool had_any = (thumbbmp != NULL);

		if (is_current) {
			if (result)
				result->refcount++; // thumbbmp's reference; the cache takes the decode's
			if (thumbbmp)
				SDL_FreeSurface(thumbbmp);
			thumbbmp = result;
			thumb_fit = task->compose == THUMB_COMPOSE_FIT;
			thumb_gen++;
		}
		thumbCacheInsert(key, result);

		if (is_current) {
			SDL_AtomicSet(&thumbchanged, 1);
			setNeedDraw(1);
			// Signal layout recalculation only if thumb presence changed
			if (had_any != (thumbbmp != NULL))
				SDL_AtomicSet(&thumbAsyncLoaded, 1);
		}

		SDL_UnlockMutex(thumbMutex);
		free(task);
	}
	return 0;
}

///////////////////////////////////////
// Public loading functions

void startLoadFolderBackground(const char* imagePath, BackgroundLoadedCallback callback) {
	LoadBackgroundTask* task = malloc(sizeof(LoadBackgroundTask));
	if (!task)
		return;

	snprintf(task->imagePath, sizeof(task->imagePath), "%s", imagePath);
	task->callback = callback;
	task->compose = THUMB_COMPOSE_BG;
	enqueueTask(&bgQueue, task);
}

void onBackgroundLoaded(SDL_Surface* surface) {
	SDL_LockMutex(bgMutex);
	// "no background" replacing "no background" is a no-op: don't set
	// folderbgchanged, or updateBackgroundLayer re-uploads the full-screen
	// black layer (~25ms) for nothing — one hitch per selection change.
	if (!surface && !folderbgbmp) {
		SDL_UnlockMutex(bgMutex);
		return;
	}
	SDL_AtomicSet(&folderbgchanged, 1);
	if (folderbgbmp)
		SDL_FreeSurface(folderbgbmp);
	if (!surface) {
		folderbgbmp = NULL;
		setNeedDraw(1);
		SDL_UnlockMutex(bgMutex);
		return;
	}
	folderbgbmp = surface;
	setNeedDraw(1);
	SDL_UnlockMutex(bgMutex);
}

// Drop the shown art (thumbMutex held): the layer loses it on the next updateBackgroundLayer.
static void dropThumb(void) {
	if (!thumbbmp)
		return;
	SDL_FreeSurface(thumbbmp);
	thumbbmp = NULL;
	thumb_gen++;
	SDL_AtomicSet(&thumbchanged, 1);
	setNeedDraw(1);
}

bool thumbPrefetch(const char* thumbpath, int compose) {
	if (!thumbpath[0] || strlen(thumbpath) >= MAX_PATH)
		return false;
	char key[THUMB_KEY_MAX];
	thumbKey(thumbpath, compose, key, sizeof(key));
	SDL_LockMutex(thumbMutex);
	for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
		if (thumb_cache[i].occupied && strcmp(thumb_cache[i].path, key) == 0) {
			SDL_UnlockMutex(thumbMutex);
			return false; // decoded already (or a remembered miss)
		}
	}
	// the selection's own picture first: nothing queued or decoding (the queue holds one task, so queueing now
	// would displace or delay it)
	if (inflightThumbKey[0] || pendingThumbKey[0]) {
		SDL_UnlockMutex(thumbMutex);
		return true;
	}
	LoadBackgroundTask* task = malloc(sizeof(LoadBackgroundTask));
	if (task) {
		snprintf(task->imagePath, sizeof(task->imagePath), "%s", thumbpath);
		task->callback = NULL;
		task->compose = compose;
		if (enqueueTask(&thumbQueue, task)) // on failure it frees the task itself
			snprintf(pendingThumbKey, sizeof(pendingThumbKey), "%s", key);
	}
	SDL_UnlockMutex(thumbMutex);
	return true;
}

bool startLoadThumb(const char* thumbpath, int compose) {
	if (strlen(thumbpath) >= MAX_PATH) {
		// longer than the task can hold: no art (a truncated path would never match its key, and would be decoded
		// again every frame)
		SDL_LockMutex(thumbMutex);
		desiredThumbPath[0] = '\0';
		dropThumb();
		SDL_UnlockMutex(thumbMutex);
		return false;
	}
	if (!thumbpath[0]) {
		// no art (a Grid, Carousel or Home screen, a tab or game without any): nothing to decode, it drops at once. Not
		// queued: "" is also the idle worker's and the empty queue's key below, which read as "already loading" and
		// left the last art up (Search's picture behind the Consoles carousel's bars)
		SDL_LockMutex(thumbMutex);
		desiredThumbPath[0] = '\0';
		if (pendingThumbKey[0]) { // a queued decode nobody wants now
			purgeQueue(&thumbQueue);
			pendingThumbKey[0] = '\0';
		}
		dropThumb();
		SDL_UnlockMutex(thumbMutex);
		return false;
	}
	char key[THUMB_KEY_MAX];
	thumbKey(thumbpath, compose, key, sizeof(key));
	SDL_LockMutex(thumbMutex);

	// Fast path: already showing the right thumb. Nothing changed: GameList_render asks every frame, and flagging a
	// change here rebuilt and re-uploaded the whole background layer every List frame. A layer someone else drew on or
	// cleared since its upload is rebuilt anyway (updateBackgroundLayer checks the layer's serial).
	if (thumbbmp && strcmp(desiredThumbPath, key) == 0) {
		SDL_UnlockMutex(thumbMutex);
		return true;
	}

	// Different item selected
	snprintf(desiredThumbPath, sizeof(desiredThumbPath), "%s", key);

	// Check cache - swap immediately if found (a remembered miss drops the art at once)
	for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
		if (thumb_cache[i].occupied &&
			strcmp(thumb_cache[i].path, key) == 0) {
			thumb_cache[i].lru_counter = ++thumb_lru_counter;
			SDL_Surface* cached = thumb_cache[i].surface;
			if (!cached) {
				dropThumb();				 // nothing to do when no art is up (the List asks every frame)
			} else if (cached != thumbbmp) { // the same surface: back on art still shown (A -> B -> A mid-load)
				cached->refcount++;			 // shared with the cache (see above)
				if (thumbbmp)
					SDL_FreeSurface(thumbbmp);
				thumbbmp = cached;
				thumb_fit = compose == THUMB_COMPOSE_FIT;
				thumb_gen++;
				SDL_AtomicSet(&thumbchanged, 1);
				setNeedDraw(1);
			}
			SDL_UnlockMutex(thumbMutex);
			return thumbbmp != NULL;
		}
	}

	// Cache miss - keep old thumb visible while loading
	bool has_thumb = (thumbbmp != NULL);
	if (has_thumb)
		SDL_AtomicSet(&thumbchanged, 1); // redraw old thumb for this frame

	// Already being decoded, or queued: don't ask again. Going back to the picture being decoded also drops the
	// one queued since (A -> B -> A decodes A once, and not B).
	if (strcmp(inflightThumbKey, key) == 0 && !inflightThumbStale) {
		if (pendingThumbKey[0]) {
			// the worker can't take the queued task meanwhile: it publishes the decode in flight under thumbMutex
			// before it dequeues again (lock order thumbMutex -> queue mutex; the worker never nests them)
			purgeQueue(&thumbQueue);
			pendingThumbKey[0] = '\0';
		}
		SDL_UnlockMutex(thumbMutex);
		return has_thumb;
	}
	if (strcmp(pendingThumbKey, key) == 0) {
		SDL_UnlockMutex(thumbMutex);
		return has_thumb;
	}

	// Enqueue for async loading
	LoadBackgroundTask* task = malloc(sizeof(LoadBackgroundTask));
	if (!task) {
		SDL_UnlockMutex(thumbMutex);
		return has_thumb;
	}
	snprintf(task->imagePath, sizeof(task->imagePath), "%s", thumbpath);
	task->callback = NULL;
	task->compose = compose;
	// queued under thumbMutex, so pendingThumbKey always names what the queue holds (it holds one: a newer task
	// replaces the older)
	if (enqueueTask(&thumbQueue, task))
		snprintf(pendingThumbKey, sizeof(pendingThumbKey), "%s", key);
	else
		pendingThumbKey[0] = '\0';
	SDL_UnlockMutex(thumbMutex);
	return has_thumb;
}

// Push the (black + folder background) surfaces to the background GPU layer
// when the loader flagged a change. Safe to call every frame. (moved from nextui.c)
void requestBackgroundReupload(void) {
	SDL_AtomicSet(&folderbgchanged, 1);
}

bool backgroundLayerPending(void) {
	return SDL_AtomicGet(&folderbgchanged) || SDL_AtomicGet(&thumbchanged);
}

// LAYER_BACKGROUND's serial right after this file's last upload: a different one means another screen (the Game
// Switcher, a transition, a return from a game, the keyboard) drew on or cleared the layer since, so it is rebuilt.
static unsigned bg_layer_serial = 0;
static bool bg_layer_known = false;

// The game art on the GPU (the List's background style): two textures, the one shown (art_front, set as the background
// layer's art) and the one the next art fills a band of rows per call (so no frame pays a whole upload), shown once
// complete. art_gen_shown: the thumbbmp generation on screen; art_gen_loading: the one being filled.
#define ART_UPLOAD_BANDS 2
static SDL_Texture* art_tex[2];
static int art_w[2], art_h[2];
static bool art_fit[2]; // the texture holds a fitted box (thumb_fit when it was filled): where artDst places it
static int art_front = -1;
static unsigned art_gen_shown = ~0u, art_gen_loading = ~0u;
static int art_rows_done = 0;

// Where the art goes: ArtBg_compose's strip, from its origin to the screen's right edge, full height (its texture is
// that at half size, the GPU scaling it back up), or ArtBg_composeFit's box (its texture is the box, 1:1)
static SDL_Rect artDst(int i) {
	if (art_fit[i])
		return ArtBg_fitRect(screen->w, screen->h, cachedFitMargin);
	int x = ArtBg_originX(screen->w, screen->h);
	return (SDL_Rect){x, 0, screen->w - x, screen->h};
}

// One step of the art upload (thumbMutex held). True once the shown art matches thumbbmp (nothing left to do).
static bool artStep(void) {
	if (thumb_gen == art_gen_shown)
		return true;
	if (!thumbbmp || thumbbmp->format->format != SDL_PIXELFORMAT_ARGB8888) {
		PLAT_setLayerArt(NULL, NULL); // no art (or none this path can upload): the layer alone
		art_front = -1;
		art_gen_shown = thumb_gen;
		art_gen_loading = ~0u;
		setNeedDraw(1);
		return true;
	}
	int back = art_front == 0 ? 1 : 0;
	if (art_gen_loading != thumb_gen) {
		if (!art_tex[back] || art_w[back] != thumbbmp->w || art_h[back] != thumbbmp->h) {
			PLAT_freeTexture(art_tex[back]);
			art_tex[back] = PLAT_textureCreate(thumbbmp->w, thumbbmp->h);
			art_w[back] = thumbbmp->w, art_h[back] = thumbbmp->h;
		}
		if (!art_tex[back])
			return true; // no texture: keep what is shown
		art_gen_loading = thumb_gen;
		art_fit[back] = thumb_fit;
		art_rows_done = 0;
	}
	int band = (thumbbmp->h + ART_UPLOAD_BANDS - 1) / ART_UPLOAD_BANDS;
	PLAT_textureUpdateRows(art_tex[back], thumbbmp, art_rows_done, band);
	art_rows_done += band;
	if (art_rows_done < thumbbmp->h)
		return false;
	art_front = back;
	art_gen_shown = thumb_gen;
	art_gen_loading = ~0u;
	SDL_Rect dst = artDst(back);
	PLAT_setLayerArt(art_tex[back], &dst);
	setNeedDraw(1);
	return true;
}

void updateBackgroundLayer(SDL_Surface* blackBG) {
	SDL_LockMutex(bgMutex);
	// The background layer holds black and the folder background; the game art is a GPU texture blended over it as
	// part of the layer (PLAT_setLayerArt: it goes whenever anything else clears or draws on the layer). A changed
	// art no longer recomposes and re-uploads the whole screen: it fills its own texture a band per call (artStep),
	// thumbchanged staying set until it shows. Lock order bgMutex -> thumbMutex is only ever taken here; the worker
	// holds thumbMutex alone, so this cannot deadlock.
	SDL_LockMutex(thumbMutex);
	// taken (and cleared) up front: a request made while this rebuild runs is kept for the next one
	bool rebuild = SDL_AtomicSet(&folderbgchanged, 0) || !bg_layer_known ||
				   PLAT_layerSerial(LAYER_BACKGROUND) != bg_layer_serial;
	if (rebuild) {
		GFX_drawOnLayer(blackBG, 0, 0, screen->w, screen->h, 1.0f, 0, LAYER_BACKGROUND);
		if (folderbgbmp)
			GFX_drawOnLayer(folderbgbmp, 0, 0, screen->w, screen->h, 1.0f, 0, LAYER_BACKGROUND);
		bg_layer_serial = PLAT_layerSerial(LAYER_BACKGROUND);
		bg_layer_known = true;
		// the redraw dropped the art: the shown one goes back over the new layer, even while a newer one is still
		// filling the other texture (it stays up until that completes, rather than a frame or two of no art);
		// artStep below drops it at once when the new art is none
		if (art_front >= 0) {
			SDL_Rect dst = artDst(art_front);
			PLAT_setLayerArt(art_tex[art_front], &dst);
		}
	}
	if (artStep())
		SDL_AtomicSet(&thumbchanged, 0);
	SDL_UnlockMutex(thumbMutex);
	SDL_UnlockMutex(bgMutex);
}

// Draw the async-loaded thumbnail on its GPU layer, or clear the thumbnail and
// scroll-text layers when `hide` is set (e.g. a confirmation dialog is open).
// (moved from nextui.c)
void renderThumbnail(int reset_changed, bool hide) {
	SDL_LockMutex(thumbMutex);
	if (hide) {
		GFX_clearLayers(LAYER_THUMBNAIL);
		GFX_clearLayers(LAYER_SCROLLTEXT);
	}
	// otherwise nothing: the art lives on LAYER_BACKGROUND, uploaded by updateBackgroundLayer (which runs first and
	// consumes thumbchanged), so the thumbnail layer stays empty
	(void)reset_changed;
	SDL_UnlockMutex(thumbMutex);
}

int thumbCheckAsyncLoaded(void) {
	return SDL_AtomicCAS(&thumbAsyncLoaded, 1, 0);
}

static void thumbCacheClear(void) {
	for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
		if (thumb_cache[i].surface)
			SDL_FreeSurface(thumb_cache[i].surface);
		thumb_cache[i] = (ThumbCacheEntry){0};
	}
	thumb_lru_counter = 0;
	desiredThumbPath[0] = '\0';
	inflightThumbKey[0] = '\0';
	inflightThumbStale = false;
	pendingThumbKey[0] = '\0';
}

///////////////////////////////////////
// Lifecycle

void initImageLoaderPool(void) {
	// Initialize shutdown flag to 0
	SDL_AtomicSet(&workerThreadsShutdown, 0);
	SDL_AtomicSet(&needDrawAtomic, 0);
	SDL_AtomicSet(&folderbgchanged, 0);
	SDL_AtomicSet(&thumbchanged, 0);

	// Cache screen properties for thread-safe access from workers
	cachedScreenFormat = screen->format->format;
	cachedScreenW = screen->w;
	cachedScreenH = screen->h;
	cachedFitMargin = SCALE1(BUTTON_MARGIN);

	bgQueue.mutex = SDL_CreateMutex();
	bgQueue.cond = SDL_CreateCond();
	thumbQueue.mutex = SDL_CreateMutex();
	thumbQueue.cond = SDL_CreateCond();
	bgMutex = SDL_CreateMutex();
	thumbMutex = SDL_CreateMutex();

	if (!bgQueue.mutex || !bgQueue.cond || !thumbQueue.mutex || !thumbQueue.cond ||
		!bgMutex || !thumbMutex) {
		fprintf(stderr, "imgloader: failed to create SDL sync primitives\n");
		return;
	}

	SDL_AtomicSet(&thumbAsyncLoaded, 0);

	bgLoadThread = SDL_CreateThread(loadWorker, "BGLoadWorker", &bgQueue);
	thumbLoadThread = SDL_CreateThread(thumbLoadWorker, "ThumbLoadWorker", &thumbQueue);
	if (!bgLoadThread || !thumbLoadThread) {
		fprintf(stderr, "imgloader: failed to create worker threads\n");
	}
}

void cleanupImageLoaderPool(void) {
	for (int i = 0; i < 2; i++) { // before GFX_quit (nextui.c calls this first)
		PLAT_freeTexture(art_tex[i]);
		art_tex[i] = NULL;
	}
	art_front = -1;
	// Signal all worker threads to exit (atomic set for thread safety)
	SDL_AtomicSet(&workerThreadsShutdown, 1);

	// Wake up all waiting threads (must hold corresponding mutex to avoid lost-wakeup race)
	if (bgQueue.mutex && bgQueue.cond) {
		SDL_LockMutex(bgQueue.mutex);
		SDL_CondSignal(bgQueue.cond);
		SDL_UnlockMutex(bgQueue.mutex);
	}
	if (thumbQueue.mutex && thumbQueue.cond) {
		SDL_LockMutex(thumbQueue.mutex);
		SDL_CondSignal(thumbQueue.cond);
		SDL_UnlockMutex(thumbQueue.mutex);
	}

	// Wait for all worker threads to finish
	if (bgLoadThread) {
		SDL_WaitThread(bgLoadThread, NULL);
		bgLoadThread = NULL;
	}
	if (thumbLoadThread) {
		SDL_WaitThread(thumbLoadThread, NULL);
		thumbLoadThread = NULL;
	}

	// Drain any residual tasks left in queues
	while (bgQueue.head) {
		TaskNode* n = bgQueue.head;
		bgQueue.head = n->next;
		free(n->task);
		free(n);
	}
	bgQueue.tail = NULL;
	bgQueue.size = 0;

	while (thumbQueue.head) {
		TaskNode* n = thumbQueue.head;
		thumbQueue.head = n->next;
		free(n->task);
		free(n);
	}
	thumbQueue.tail = NULL;
	thumbQueue.size = 0;

	// Clear thumbnail cache
	thumbCacheClear();

	// Acquire and release each mutex before destroying to ensure no thread is in a critical section
	// This creates a memory barrier and ensures proper synchronization
	if (bgQueue.mutex) {
		SDL_LockMutex(bgQueue.mutex);
		SDL_UnlockMutex(bgQueue.mutex);
	}
	if (thumbQueue.mutex) {
		SDL_LockMutex(thumbQueue.mutex);
		SDL_UnlockMutex(thumbQueue.mutex);
	}
	if (bgMutex) {
		SDL_LockMutex(bgMutex);
		SDL_UnlockMutex(bgMutex);
	}
	if (thumbMutex) {
		SDL_LockMutex(thumbMutex);
		SDL_UnlockMutex(thumbMutex);
	}

	// Destroy mutexes and condition variables
	if (bgQueue.mutex)
		SDL_DestroyMutex(bgQueue.mutex);
	if (thumbQueue.mutex)
		SDL_DestroyMutex(thumbQueue.mutex);
	if (bgMutex)
		SDL_DestroyMutex(bgMutex);
	if (thumbMutex)
		SDL_DestroyMutex(thumbMutex);

	if (bgQueue.cond)
		SDL_DestroyCond(bgQueue.cond);
	if (thumbQueue.cond)
		SDL_DestroyCond(thumbQueue.cond);

	// Set pointers to NULL after destruction
	bgQueue = (TaskQueue){0};
	thumbQueue = (TaskQueue){0};
	bgMutex = NULL;
	thumbMutex = NULL;
}
