#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "iv_defines.h"
#include "iv_loader.h"
#include "iv_probe.h"
#include "ui_image.h"

// Single worker thread modeled on nextui/imgloader.c: request slots (one per
// purpose, latest-wins) + a fixed result ring, both guarded by one mutex.

#define RESULT_RING_SIZE 8 // >= 3 outstanding purposes in practice

typedef struct {
	char path[512];
	bool valid;
} PendingSlot;

typedef struct {
	SDL_mutex* mutex;
	SDL_cond* cond;
	SDL_Thread* thread;
	SDL_atomic_t shutdown;

	PendingSlot pending[IV_PURPOSE_COUNT]; // guarded by mutex

	IvLoadResult results[RESULT_RING_SIZE]; // guarded by mutex
	int result_head;
	int result_count;

	// Set once in IvLoader_init before the worker thread starts; read-only
	// afterward, so safe for the worker to read without the mutex.
	Uint32 screen_format;
	int preview_max_w, preview_max_h;
} LoaderState;

static LoaderState g_loader;

static bool anyPendingLocked(void) {
	for (int i = 0; i < IV_PURPOSE_COUNT; i++) {
		if (g_loader.pending[i].valid)
			return true;
	}
	return false;
}

// Decode one request off the worker thread. Never touches g_loader.mutex -
// the caller has already copied path/purpose out of the pending slot.
static IvLoadResult decodeRequest(const char* path, IvLoadPurpose purpose) {
	IvLoadResult r = {0};
	snprintf(r.path, sizeof(r.path), "%s", path);
	r.purpose = purpose;

	struct stat st;
	if (stat(path, &st) == 0 && st.st_size > IV_MAX_FILE_BYTES) {
		r.status = IV_LOAD_ERR_TOO_LARGE;
		return r;
	}

	// Decode-bomb guard: reject on header-probed dimensions before IMG_Load
	// ever touches pixel data.
	int probe_w = 0, probe_h = 0;
	bool probed = IvProbe_dimensions(path, &probe_w, &probe_h);
	if (probed && (long)probe_w * probe_h > IV_MAX_PIXELS) {
		r.status = IV_LOAD_ERR_TOO_LARGE;
		return r;
	}

	SDL_Surface* raw = IMG_Load(path);
	if (!raw) {
		r.status = IV_LOAD_ERR_DECODE;
		return r;
	}

	// Backstop: probe couldn't read this format's header, so check the
	// decoded size before it goes any further.
	if (!probed && (long)raw->w * raw->h > IV_MAX_PIXELS) {
		SDL_FreeSurface(raw);
		r.status = IV_LOAD_ERR_TOO_LARGE;
		return r;
	}

	SDL_Surface* converted = SDL_ConvertSurfaceFormat(raw, g_loader.screen_format, 0);
	SDL_FreeSurface(raw);
	if (!converted) {
		r.status = IV_LOAD_ERR_DECODE;
		return r;
	}

	r.orig_w = converted->w;
	r.orig_h = converted->h;

	if (purpose == IV_LOAD_PREVIEW) {
		int new_w, new_h;
		UI_calcImageFit(converted->w, converted->h, g_loader.preview_max_w,
						g_loader.preview_max_h, &new_w, &new_h);
		if (new_w > 0 && new_h > 0 && (new_w < converted->w || new_h < converted->h)) {
			SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(
				0, new_w, new_h,
				converted->format->BitsPerPixel, converted->format->format);
			if (scaled) {
				SDL_BlitScaled(converted, NULL, scaled, NULL);
				SDL_FreeSurface(converted);
				converted = scaled;
			}
		}
	}

	r.status = IV_LOAD_OK;
	r.surface = converted;
	return r;
}

static int loaderWorker(void* arg) {
	(void)arg;
	for (;;) {
		SDL_LockMutex(g_loader.mutex);
		while (!SDL_AtomicGet(&g_loader.shutdown) && !anyPendingLocked())
			SDL_CondWait(g_loader.cond, g_loader.mutex);
		if (SDL_AtomicGet(&g_loader.shutdown)) {
			SDL_UnlockMutex(g_loader.mutex);
			break;
		}

		IvLoadPurpose purpose;
		if (g_loader.pending[IV_LOAD_FULL].valid)
			purpose = IV_LOAD_FULL;
		else if (g_loader.pending[IV_LOAD_PREVIEW].valid)
			purpose = IV_LOAD_PREVIEW;
		else
			purpose = IV_LOAD_PREFETCH;

		char path[512];
		snprintf(path, sizeof(path), "%s", g_loader.pending[purpose].path);
		g_loader.pending[purpose].valid = false;
		SDL_UnlockMutex(g_loader.mutex);

		IvLoadResult result = decodeRequest(path, purpose);

		SDL_LockMutex(g_loader.mutex);
		if (g_loader.result_count < RESULT_RING_SIZE) {
			int idx = (g_loader.result_head + g_loader.result_count) % RESULT_RING_SIZE;
			g_loader.results[idx] = result;
			g_loader.result_count++;
		} else if (result.surface) {
			// Cannot happen in practice (<= 3 outstanding purposes vs an
			// 8-slot ring), but don't leak if it ever does.
			SDL_FreeSurface(result.surface);
		}
		SDL_UnlockMutex(g_loader.mutex);
	}
	return 0;
}

void IvLoader_init(SDL_Surface* screen, int preview_max_w, int preview_max_h) {
	g_loader.screen_format = screen->format->format;
	g_loader.preview_max_w = preview_max_w;
	g_loader.preview_max_h = preview_max_h;
	SDL_AtomicSet(&g_loader.shutdown, 0);

	g_loader.mutex = SDL_CreateMutex();
	g_loader.cond = SDL_CreateCond();
	if (!g_loader.mutex || !g_loader.cond) {
		fprintf(stderr, "iv_loader: failed to create SDL sync primitives\n");
		// Leave no half-initialized state: every other entry point treats
		// a NULL mutex as "not initialized".
		if (g_loader.mutex)
			SDL_DestroyMutex(g_loader.mutex);
		if (g_loader.cond)
			SDL_DestroyCond(g_loader.cond);
		g_loader.mutex = NULL;
		g_loader.cond = NULL;
		return;
	}

	g_loader.thread = SDL_CreateThread(loaderWorker, "IvLoadWorker", NULL);
	if (!g_loader.thread)
		fprintf(stderr, "iv_loader: failed to create worker thread\n");
}

void IvLoader_quit(void) {
	if (!g_loader.mutex)
		return;

	// Signal shutdown under the mutex so the worker can't miss the wakeup
	// between its predicate check and the CondWait.
	SDL_AtomicSet(&g_loader.shutdown, 1);
	SDL_LockMutex(g_loader.mutex);
	SDL_CondSignal(g_loader.cond);
	SDL_UnlockMutex(g_loader.mutex);

	if (g_loader.thread) {
		SDL_WaitThread(g_loader.thread, NULL);
		g_loader.thread = NULL;
	}

	// Drain the result ring so no decoded surface outlives the loader; the
	// lock here also stands in for imgloader's separate pre-destroy barrier
	// (the worker is already joined, so this is uncontended).
	SDL_LockMutex(g_loader.mutex);
	for (int i = 0; i < g_loader.result_count; i++) {
		int idx = (g_loader.result_head + i) % RESULT_RING_SIZE;
		if (g_loader.results[idx].surface)
			SDL_FreeSurface(g_loader.results[idx].surface);
	}
	g_loader.result_head = 0;
	g_loader.result_count = 0;
	SDL_UnlockMutex(g_loader.mutex);

	SDL_DestroyMutex(g_loader.mutex);
	SDL_DestroyCond(g_loader.cond);
	g_loader = (LoaderState){0};
}

void IvLoader_request(const char* path, IvLoadPurpose purpose) {
	if (!g_loader.mutex || purpose < 0 || purpose >= IV_PURPOSE_COUNT)
		return;
	SDL_LockMutex(g_loader.mutex);
	snprintf(g_loader.pending[purpose].path, sizeof(g_loader.pending[purpose].path), "%s", path);
	g_loader.pending[purpose].valid = true;
	SDL_CondSignal(g_loader.cond);
	SDL_UnlockMutex(g_loader.mutex);
}

bool IvLoader_poll(IvLoadResult* out) {
	if (!g_loader.mutex)
		return false;
	SDL_LockMutex(g_loader.mutex);
	if (g_loader.result_count == 0) {
		SDL_UnlockMutex(g_loader.mutex);
		return false;
	}
	*out = g_loader.results[g_loader.result_head];
	g_loader.result_head = (g_loader.result_head + 1) % RESULT_RING_SIZE;
	g_loader.result_count--;
	SDL_UnlockMutex(g_loader.mutex);
	return true;
}
