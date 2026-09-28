#ifndef MA_HWRENDER_H
#define MA_HWRENDER_H
// libretro hardware-render (GPU core) support: the libretro side. GL work goes
// through PLAT_HWR_* (hwr_plat.h, implemented in generic_video.c).
#include <stdbool.h>
#include <stddef.h>
#include "libretro.h"

bool HWR_isSupportedContext(enum retro_hw_context_type type);
bool HWR_setCallback(struct retro_hw_render_callback* cb);
bool HWR_active(void);
bool HWR_contextFailed(void); // a GPU core was accepted but its context could not be started
void HWR_makeCurrent(void);	  // before any core entry point that may touch GL
void HWR_contextReset(unsigned max_w, unsigned max_h);
void HWR_contextDestroy(void);
void HWR_beforeRun(void);
bool HWR_submitFrame(const void* data, unsigned w, unsigned h);
void HWR_clampFrame(unsigned* w, unsigned* h);
int HWR_frameFlip(void);
void HWR_countAudio(size_t frames);
void HWR_reset(void);
#endif
