#include <stdio.h>
#include <string.h>
#include "ma_hwrender.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

// --- PLAT_HWR_* stubs ---
static unsigned stub_fbo = 7, stub_create_w, stub_create_h, stub_setframe_w, stub_setframe_h;
static int stub_create_calls, stub_destroy_calls, stub_current_calls, stub_restore_calls, stub_setframe_flip = -1;
unsigned PLAT_HWR_create(unsigned w, unsigned h, int depth, int stencil) {
	(void)depth;
	(void)stencil;
	stub_create_calls++;
	stub_create_w = w;
	stub_create_h = h;
	return stub_fbo;
}
void PLAT_HWR_destroy(void) {
	stub_destroy_calls++;
}
void PLAT_HWR_makeCurrent(void) {
	stub_current_calls++;
}
void PLAT_HWR_setFrame(unsigned w, unsigned h, int flip) {
	stub_setframe_w = w;
	stub_setframe_h = h;
	stub_setframe_flip = flip;
}
void PLAT_HWR_restoreFrontendState(void) {
	stub_restore_calls++;
}
void* PLAT_HWR_getProcAddress(const char* sym) {
	(void)sym;
	return (void*)0x1234;
}
int PLAT_HWR_maxTextureSize(void) {
	return 4096;
}

// --- fake core callbacks ---
static int core_reset_calls, core_destroy_calls;
static void core_reset(void) {
	core_reset_calls++;
}
static void core_destroy(void) {
	core_destroy_calls++;
}

static struct retro_hw_render_callback make_cb(enum retro_hw_context_type t) {
	struct retro_hw_render_callback cb;
	memset(&cb, 0, sizeof(cb));
	cb.context_type = t;
	cb.context_reset = core_reset;
	cb.context_destroy = core_destroy;
	cb.depth = true;
	cb.stencil = true;
	cb.bottom_left_origin = true;
	return cb;
}

static void reset_all(void) {
	HWR_reset();
	stub_fbo = 7;
	stub_create_calls = stub_destroy_calls = stub_current_calls = stub_restore_calls = 0;
	stub_setframe_flip = -1;
	core_reset_calls = core_destroy_calls = 0;
}

int main(void) {
	// 1. context acceptance
	CHECK(HWR_isSupportedContext(RETRO_HW_CONTEXT_OPENGLES2), "GLES2 accepted");
	CHECK(HWR_isSupportedContext(RETRO_HW_CONTEXT_OPENGLES3), "GLES3 accepted");
	CHECK(HWR_isSupportedContext(RETRO_HW_CONTEXT_OPENGLES_VERSION), "GLES_VERSION accepted");
	CHECK(!HWR_isSupportedContext(RETRO_HW_CONTEXT_VULKAN), "Vulkan refused");
	CHECK(!HWR_isSupportedContext(RETRO_HW_CONTEXT_OPENGL_CORE), "GL core refused");
	CHECK(!HWR_isSupportedContext(RETRO_HW_CONTEXT_OPENGL), "GL compat refused");
	CHECK(!HWR_isSupportedContext(RETRO_HW_CONTEXT_NONE), "NONE refused");

	// 2. Vulkan request is refused and leaves the path inactive
	reset_all();
	{
		struct retro_hw_render_callback cb = make_cb(RETRO_HW_CONTEXT_VULKAN);
		CHECK(!HWR_setCallback(&cb), "setCallback(Vulkan) returns false");
		HWR_contextReset(640, 480);
		CHECK(!HWR_active(), "Vulkan: inactive after reset");
		CHECK(core_reset_calls == 0, "Vulkan: core context_reset never called");
		CHECK(!HWR_contextFailed(), "Vulkan refused: not a failure (software path)");
	}

	// 3. GLES3 request fills frontend hooks; reset creates FBO then calls core
	reset_all();
	{
		struct retro_hw_render_callback cb = make_cb(RETRO_HW_CONTEXT_OPENGLES3);
		CHECK(HWR_setCallback(&cb), "setCallback(GLES3) returns true");
		CHECK(cb.get_current_framebuffer != NULL, "get_current_framebuffer filled");
		CHECK(cb.get_proc_address != NULL, "get_proc_address filled");
		CHECK(!HWR_active(), "not active before context reset");
		HWR_contextReset(640, 480);
		CHECK(HWR_active(), "active after context reset");
		CHECK(!HWR_contextFailed(), "success: not failed");
		CHECK(stub_create_calls == 1 && stub_create_w == 640 && stub_create_h == 480, "FBO created at max geometry");
		CHECK(core_reset_calls == 1, "core context_reset called once");
		CHECK(cb.get_current_framebuffer() == 7, "get_current_framebuffer returns FBO");
		CHECK(cb.get_proc_address("glDrawArrays") == (retro_proc_address_t)0x1234, "get_proc_address forwards");
	}

	// 4. FBO creation failure: core never told the context is ready
	reset_all();
	{
		struct retro_hw_render_callback cb = make_cb(RETRO_HW_CONTEXT_OPENGLES3);
		HWR_setCallback(&cb);
		stub_fbo = 0;
		HWR_contextReset(640, 480);
		CHECK(!HWR_active(), "FBO failure: inactive");
		CHECK(core_reset_calls == 0, "FBO failure: core context_reset not called");
		CHECK(HWR_contextFailed(), "FBO failure: reported as failed");
	}

	// 5. zero / oversized geometry
	reset_all();
	{
		struct retro_hw_render_callback cb = make_cb(RETRO_HW_CONTEXT_OPENGLES3);
		HWR_setCallback(&cb);
		HWR_contextReset(0, 0);
		CHECK(stub_create_w == 640 && stub_create_h == 480, "zero geometry falls back to 640x480");
		HWR_contextDestroy();
		HWR_setCallback(&cb);
		HWR_contextReset(8000, 3000);
		CHECK(stub_create_w == 4096 && stub_create_h == 3000, "geometry clamped to max texture size");
	}

	// 6. frame submit: valid FB blits with flip + restores state; NULL dupes; inactive ignores
	reset_all();
	{
		CHECK(!HWR_submitFrame(RETRO_HW_FRAME_BUFFER_VALID, 640, 480), "inactive: submit returns false");
		struct retro_hw_render_callback cb = make_cb(RETRO_HW_CONTEXT_OPENGLES3);
		HWR_setCallback(&cb);
		HWR_contextReset(640, 480);
		CHECK(!HWR_submitFrame(NULL, 640, 480), "dupe before any frame: nothing to present");
		CHECK(HWR_submitFrame(RETRO_HW_FRAME_BUFFER_VALID, 320, 240), "valid frame presented");
		CHECK(stub_setframe_w == 320 && stub_setframe_h == 240 && stub_setframe_flip == 1, "blit 320x240 flipped");
		CHECK(stub_restore_calls == 1, "frontend state restored after blit");
		CHECK(HWR_submitFrame(NULL, 320, 240), "dupe after a frame re-presents");
		CHECK(stub_restore_calls == 2, "dupe also restores frontend state (core ran this frame)");
	}

	// 7. clamp + flip flag
	reset_all();
	{
		struct retro_hw_render_callback cb = make_cb(RETRO_HW_CONTEXT_OPENGLES3);
		cb.bottom_left_origin = false;
		HWR_setCallback(&cb);
		HWR_contextReset(640, 480);
		unsigned w = 1280, h = 960;
		HWR_clampFrame(&w, &h);
		CHECK(w == 640 && h == 480, "oversized frame clamped to FBO");
		CHECK(HWR_frameFlip() == 0, "top-left origin: no flip");
	}

	// 8. beforeRun makes the game context current only when active; destroy order
	reset_all();
	{
		HWR_beforeRun();
		HWR_makeCurrent();
		CHECK(stub_current_calls == 0, "inactive: beforeRun/makeCurrent are no-ops");
		struct retro_hw_render_callback cb = make_cb(RETRO_HW_CONTEXT_OPENGLES3);
		HWR_setCallback(&cb);
		HWR_contextReset(640, 480);
		HWR_beforeRun();
		CHECK(stub_current_calls >= 1, "active: beforeRun makes context current");
		int before = stub_current_calls;
		HWR_makeCurrent();
		CHECK(stub_current_calls == before + 1, "active: makeCurrent makes context current");
		HWR_contextDestroy();
		CHECK(core_destroy_calls == 1 && stub_destroy_calls == 1, "destroy: core then platform, once");
		CHECK(!HWR_active(), "inactive after destroy");
		HWR_contextDestroy();
		CHECK(core_destroy_calls == 1 && stub_destroy_calls == 1, "second destroy is a no-op");
	}

	printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
	return fails ? 1 : 0;
}
