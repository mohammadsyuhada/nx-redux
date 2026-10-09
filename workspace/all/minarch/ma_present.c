// ma_present.c - see ma_present.h.
#include "ma_present.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gbalink.h"
#include "ma_hwrender.h"
#include "ma_internal.h"

typedef struct {
	GFX_Renderer r;
	void* buf;
	size_t cap;
} Slot;

static pthread_t thread;
static bool running;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t frame_mu = PTHREAD_MUTEX_INITIALIZER; // held around PLAT_GL_Swap
static Slot slots[2];
static int pending = -1;	// slot waiting to be presented
static int presenting = -1; // slot the thread is drawing
static bool stop_req;
static uint64_t last_submit_us;

static uint64_t mono_us(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static void* present_main(void* arg) {
	(void)arg;
	for (;;) {
		pthread_mutex_lock(&mu);
		while (!stop_req && pending < 0)
			pthread_cond_wait(&cv, &mu);
		if (stop_req) {
			pthread_mutex_unlock(&mu);
			break;
		}
		presenting = pending;
		pending = -1;
		pthread_mutex_unlock(&mu);

		pthread_mutex_lock(&frame_mu);
		GFX_blitRenderer(&slots[presenting].r);
		PLAT_GL_Swap();
		pthread_mutex_unlock(&frame_mu);

		pthread_mutex_lock(&mu);
		presenting = -1;
		pthread_mutex_unlock(&mu);
	}
	PLAT_GL_releaseCurrent(); // the main thread takes the context back
	return NULL;
}

static bool wanted(void) {
	// Software video on the vsync-paced path only: GPU cores, core-fps and
	// emulated pacing, and fast-forward keep their own presenting.
	return !HWR_active() && !use_core_fps && !fast_forward && GBALink_lockstepFollowerBusy();
}

static void start(void) {
	pending = presenting = -1;
	stop_req = false;
	last_submit_us = 0;
	PLAT_GL_releaseCurrent();
	if (pthread_create(&thread, NULL, present_main, NULL) != 0) {
		LOG_error("present: thread start failed\n");
		return;
	}
	running = true;
	LOG_info("present: follower link busy, presenting on a thread\n");
}

void Present_stop(void) {
	if (!running)
		return;
	pthread_mutex_lock(&mu);
	stop_req = true;
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&mu);
	pthread_join(thread, NULL);
	running = false;
	LOG_info("present: back on the main thread\n");
}

bool Present_update(void) {
	bool want = wanted();
	if (want && !running)
		start();
	else if (!want && running)
		Present_stop();
	return running;
}

void Present_submit(const GFX_Renderer* r) {
	size_t need = (size_t)r->src_p * (size_t)r->true_h;
	pthread_mutex_lock(&mu);
	int w = presenting == 0 ? 1 : presenting == 1 ? 0
							  : pending >= 0	  ? pending
												  : 0;
	Slot* s = &slots[w];
	if (s->cap < need) {
		void* b = realloc(s->buf, need);
		if (!b) {
			pthread_mutex_unlock(&mu);
			return; // frame dropped
		}
		s->buf = b;
		s->cap = need;
	}
	memcpy(s->buf, r->src, need);
	s->r = *r;
	s->r.src = s->buf;
	pending = w;
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&mu);

	GFX_GL_noteFrame(); // current_fps (audio ratio) follows the emulated rate

	// Runaway guard: the lockstep bound paces a busy follower; if the leader
	// goes quiet the follower would otherwise run unthrottled until the busy
	// window closes.
	uint64_t now = mono_us();
	uint64_t min_us = (uint64_t)(1000000.0 / ((core.fps > 0 ? core.fps : 60.0) * 1.25));
	if (last_submit_us && now - last_submit_us < min_us) {
		usleep((useconds_t)(min_us - (now - last_submit_us)));
		now = mono_us();
	}
	last_submit_us = now;
}

bool Present_lockFrameState(void) {
	if (!running)
		return true;
	return pthread_mutex_trylock(&frame_mu) == 0;
}

void Present_unlockFrameState(void) {
	if (running)
		pthread_mutex_unlock(&frame_mu);
}
