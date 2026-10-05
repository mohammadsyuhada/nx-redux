// Background decode of bundled menu art (artloader.h). MenuArt_load touches no shared state (IMG_Load, a format
// conversion and AreaScale on its own surfaces), so it runs on the worker as is; the caches it feeds (MenuArt's,
// the controller art's) stay main-thread only and only ever take finished surfaces from here.

#include "artloader.h"
#include "menuart.h"

#include <string.h>

#define ART_JOBS 48

typedef enum { JOB_FREE = 0,
			   JOB_QUEUED,
			   JOB_WORKING,
			   JOB_READY } JobState;

typedef struct {
	char file[64];
	int box_w, box_h, prio;
	JobState state;
	bool dropped; // cleared while decoding: free the result instead of handing it over
	bool failed;
	SDL_Surface* surface;
} ArtJob;

static ArtJob jobs[ART_JOBS];
static SDL_mutex* lock;
static SDL_cond* wake;
static SDL_Thread* worker;
static bool quitting;

static bool same(const ArtJob* j, const char* file, int w, int h) {
	return j->box_w == w && j->box_h == h && strcmp(j->file, file) == 0;
}

static int workerMain(void* arg) {
	(void)arg;
	SDL_LockMutex(lock);
	while (!quitting) {
		ArtJob* next = NULL;
		for (int i = 0; i < ART_JOBS; i++)
			if (jobs[i].state == JOB_QUEUED && (!next || jobs[i].prio < next->prio))
				next = &jobs[i];
		if (!next) {
			SDL_CondWait(wake, lock);
			continue;
		}
		next->state = JOB_WORKING;
		char file[64];
		snprintf(file, sizeof(file), "%s", next->file);
		int w = next->box_w, h = next->box_h;
		SDL_UnlockMutex(lock);
		SDL_Surface* s = w == 0 && h == 0 ? MenuArt_loadHalf(file) : MenuArt_load(file, w, h);
		SDL_LockMutex(lock);
		if (next->dropped || quitting) {
			if (s)
				SDL_FreeSurface(s);
			memset(next, 0, sizeof(*next));
		} else {
			next->surface = s;
			next->failed = s == NULL;
			next->state = JOB_READY;
		}
	}
	SDL_UnlockMutex(lock);
	return 0;
}

static bool ensureStarted(void) {
	if (worker)
		return true;
	if (!lock)
		lock = SDL_CreateMutex();
	if (!wake)
		wake = SDL_CreateCond();
	if (!lock || !wake)
		return false;
	quitting = false;
	worker = SDL_CreateThread(workerMain, "ArtLoader", NULL);
	return worker != NULL;
}

void ArtLoader_request(const char* file, int box_w, int box_h, int prio) {
	bool half = box_w == 0 && box_h == 0;
	if (!file || !file[0] || (!half && (box_w < 1 || box_h < 1)) || strlen(file) >= sizeof(jobs[0].file) ||
		!ensureStarted())
		return;
	SDL_LockMutex(lock);
	ArtJob* slot = NULL;
	for (int i = 0; i < ART_JOBS; i++) {
		ArtJob* j = &jobs[i];
		if (j->state != JOB_FREE && !j->dropped && same(j, file, box_w, box_h)) {
			if (j->state == JOB_QUEUED && prio < j->prio)
				j->prio = prio;
			SDL_UnlockMutex(lock);
			return;
		}
		if (j->state == JOB_FREE && !slot)
			slot = j;
	}
	if (!slot) { // full: replace the furthest queued or never-taken job, if this one is nearer
		for (int i = 0; i < ART_JOBS; i++)
			if ((jobs[i].state == JOB_QUEUED || (jobs[i].state == JOB_READY && !jobs[i].dropped)) &&
				(!slot || jobs[i].prio > slot->prio))
				slot = &jobs[i];
		if (!slot || slot->prio <= prio) {
			SDL_UnlockMutex(lock);
			return;
		}
		if (slot->surface)
			SDL_FreeSurface(slot->surface);
	}
	memset(slot, 0, sizeof(*slot));
	snprintf(slot->file, sizeof(slot->file), "%s", file);
	slot->box_w = box_w, slot->box_h = box_h, slot->prio = prio;
	slot->state = JOB_QUEUED;
	SDL_CondSignal(wake);
	SDL_UnlockMutex(lock);
}

SDL_Surface* ArtLoader_take(const char* file, int box_w, int box_h, bool* failed) {
	if (failed)
		*failed = false;
	if (!file || !lock)
		return NULL;
	SDL_Surface* s = NULL;
	SDL_LockMutex(lock);
	for (int i = 0; i < ART_JOBS; i++) {
		ArtJob* j = &jobs[i];
		if (j->state == JOB_READY && same(j, file, box_w, box_h)) {
			s = j->surface;
			if (failed)
				*failed = j->failed;
			memset(j, 0, sizeof(*j));
			break;
		}
	}
	SDL_UnlockMutex(lock);
	return s;
}

void ArtLoader_clear(void) {
	if (!lock)
		return;
	SDL_LockMutex(lock);
	for (int i = 0; i < ART_JOBS; i++) {
		ArtJob* j = &jobs[i];
		if (j->state == JOB_WORKING) {
			j->dropped = true;
			continue;
		}
		if (j->surface)
			SDL_FreeSurface(j->surface);
		memset(j, 0, sizeof(*j));
	}
	SDL_UnlockMutex(lock);
}

void ArtLoader_quit(void) {
	if (!worker)
		return;
	SDL_LockMutex(lock);
	quitting = true;
	SDL_CondSignal(wake);
	SDL_UnlockMutex(lock);
	SDL_WaitThread(worker, NULL);
	worker = NULL;
	ArtLoader_clear();
}
