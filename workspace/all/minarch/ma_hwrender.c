#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "ma_hwrender.h"
#include "hwr_plat.h"
#include "ma_emutime.h"

#define HWR_DEFAULT_W 640
#define HWR_DEFAULT_H 480
#define HWR_STATS_MS 5000

static struct {
	bool requested; // a supported callback was accepted
	bool active;	// ...and its context was reset successfully
	struct retro_hw_render_callback cb;
	unsigned fbo, fbo_w, fbo_h;
	bool has_frame;
	bool warned_clamp;
	unsigned stat_runs, stat_frames;
	unsigned long long stat_audio_base;
	bool stats_on; // NX_HWR_STATS=1 // EmuTime total at the window start: emulation speed = audio rate / sample rate
	unsigned long stat_start_ms;
} hwr;

static unsigned long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)ts.tv_sec * 1000UL + (unsigned long)(ts.tv_nsec / 1000000L);
}

bool HWR_isSupportedContext(enum retro_hw_context_type type) {
	return type == RETRO_HW_CONTEXT_OPENGLES2 || type == RETRO_HW_CONTEXT_OPENGLES3 ||
		   type == RETRO_HW_CONTEXT_OPENGLES_VERSION;
}

static uintptr_t hwr_get_current_framebuffer(void) {
	return hwr.fbo;
}

static retro_proc_address_t hwr_get_proc_address(const char* sym) {
	return (retro_proc_address_t)PLAT_HWR_getProcAddress(sym);
}

bool HWR_setCallback(struct retro_hw_render_callback* cb) {
	if (!cb || !HWR_isSupportedContext(cb->context_type)) {
		printf("[HWR] refusing hw context type %d\n", cb ? (int)cb->context_type : -1);
		fflush(stdout);
		return false;
	}
	cb->get_current_framebuffer = hwr_get_current_framebuffer;
	cb->get_proc_address = hwr_get_proc_address;
	hwr.cb = *cb;
	hwr.requested = true;
	printf("[HWR] accepted context type %d v%u.%u depth=%d stencil=%d bottom_left=%d\n",
		   (int)cb->context_type, cb->version_major, cb->version_minor, cb->depth, cb->stencil,
		   cb->bottom_left_origin);
	fflush(stdout);
	return true;
}

bool HWR_active(void) {
	return hwr.active;
}

bool HWR_contextFailed(void) {
	return hwr.requested && !hwr.active;
}

void HWR_makeCurrent(void) {
	if (hwr.active)
		PLAT_HWR_makeCurrent();
}

void HWR_contextReset(unsigned max_w, unsigned max_h) {
	if (!hwr.requested || hwr.active)
		return;
	if (max_w == 0 || max_h == 0) {
		max_w = HWR_DEFAULT_W;
		max_h = HWR_DEFAULT_H;
	}
	int gl_max = PLAT_HWR_maxTextureSize();
	if (gl_max > 0) {
		if (max_w > (unsigned)gl_max)
			max_w = gl_max;
		if (max_h > (unsigned)gl_max)
			max_h = gl_max;
	}
	hwr.fbo = PLAT_HWR_create(max_w, max_h, hwr.cb.depth, hwr.cb.stencil);
	if (!hwr.fbo) {
		printf("[HWR] FBO %ux%u creation failed; core not started\n", max_w, max_h);
		fflush(stdout);
		return;
	}
	hwr.fbo_w = max_w;
	hwr.fbo_h = max_h;
	hwr.active = true;
	hwr.has_frame = false;
	hwr.stat_start_ms = now_ms();
	// the 5 s [HWR] stats line is a measuring aid (spike runs, benchmarks)
	const char* stats = getenv("NX_HWR_STATS");
	hwr.stats_on = stats != NULL && strcmp(stats, "1") == 0;
	hwr.stat_audio_base = EmuTime_totalFrames();
	if (hwr.cb.context_reset)
		hwr.cb.context_reset();
	printf("[HWR] context reset, FBO %u (%ux%u)\n", hwr.fbo, max_w, max_h);
	fflush(stdout);
}

void HWR_contextDestroy(void) {
	if (!hwr.active)
		return;
	PLAT_HWR_makeCurrent();
	if (hwr.cb.context_destroy)
		hwr.cb.context_destroy();
	PLAT_HWR_destroy();
	hwr.active = false;
	hwr.requested = false;
	hwr.fbo = 0;
	hwr.has_frame = false;
}

static void hwr_stats(void) {
	if (!hwr.stats_on)
		return;
	unsigned long now = now_ms();
	unsigned long elapsed = now - hwr.stat_start_ms;
	if (elapsed < HWR_STATS_MS)
		return;
	printf("[HWR] %.1fs: runs=%u frames=%u -> %.1f runs/s %.1f frames/s audio %.0f/s\n", elapsed / 1000.0,
		   hwr.stat_runs, hwr.stat_frames, hwr.stat_runs * 1000.0 / elapsed,
		   hwr.stat_frames * 1000.0 / elapsed, (EmuTime_totalFrames() - hwr.stat_audio_base) * 1000.0 / elapsed);
	fflush(stdout);
	hwr.stat_runs = hwr.stat_frames = 0;
	hwr.stat_audio_base = EmuTime_totalFrames();
	hwr.stat_start_ms = now;
}

void HWR_beforeRun(void) {
	if (!hwr.active)
		return;
	// The in-game menu draws through SDL's renderer, which has its own GL
	// context: make the game context current again before the core draws.
	PLAT_HWR_makeCurrent();
	hwr.stat_runs++;
	hwr_stats();
}

void HWR_clampFrame(unsigned* w, unsigned* h) {
	if (*w > hwr.fbo_w || *h > hwr.fbo_h) {
		if (!hwr.warned_clamp) {
			printf("[HWR] frame %ux%u exceeds FBO %ux%u, clamping\n", *w, *h, hwr.fbo_w, hwr.fbo_h);
			fflush(stdout);
			hwr.warned_clamp = true;
		}
		if (*w > hwr.fbo_w)
			*w = hwr.fbo_w;
		if (*h > hwr.fbo_h)
			*h = hwr.fbo_h;
	}
}

bool HWR_growFramebuffer(unsigned max_w, unsigned max_h) {
	if (!hwr.active)
		return false;
	unsigned w = max_w > hwr.fbo_w ? max_w : hwr.fbo_w;
	unsigned h = max_h > hwr.fbo_h ? max_h : hwr.fbo_h;
	int gl_max = PLAT_HWR_maxTextureSize();
	if (gl_max > 0) {
		if (w > (unsigned)gl_max)
			w = gl_max;
		if (h > (unsigned)gl_max)
			h = gl_max;
	}
	if (w == hwr.fbo_w && h == hwr.fbo_h)
		return false;
	if (!PLAT_HWR_resize(w, h)) {
		printf("[HWR] FBO grow to %ux%u failed, keeping %ux%u\n", w, h, hwr.fbo_w, hwr.fbo_h);
		fflush(stdout);
		return false;
	}
	printf("[HWR] FBO grown %ux%u -> %ux%u\n", hwr.fbo_w, hwr.fbo_h, w, h);
	fflush(stdout);
	hwr.fbo_w = w;
	hwr.fbo_h = h;
	hwr.warned_clamp = false;
	return true;
}

int HWR_frameFlip(void) {
	return hwr.cb.bottom_left_origin ? 1 : 0;
}

bool HWR_submitFrame(const void* data, unsigned w, unsigned h) {
	if (!hwr.active)
		return false;
	if (data != RETRO_HW_FRAME_BUFFER_VALID) {
		// dupe: re-present the last blitted frame. The core still ran this
		// frame and may have left GL state bound, so reset ours either way.
		if (hwr.has_frame)
			PLAT_HWR_restoreFrontendState();
		return hwr.has_frame;
	}
	HWR_clampFrame(&w, &h);
	PLAT_HWR_setFrame(w, h, HWR_frameFlip());
	PLAT_HWR_restoreFrontendState();
	hwr.has_frame = true;
	hwr.stat_frames++;
	return true;
}

void HWR_reset(void) {
	memset(&hwr, 0, sizeof(hwr));
}
