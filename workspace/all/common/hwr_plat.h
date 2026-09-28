#ifndef HWR_PLAT_H
#define HWR_PLAT_H
// Platform side of minarch's libretro GPU-render path (generic_video.c).
// Plain C types only so the libretro-side module stays host-testable.
unsigned PLAT_HWR_create(unsigned w, unsigned h, int depth, int stencil);
void PLAT_HWR_destroy(void);
void PLAT_HWR_makeCurrent(void);
void PLAT_HWR_setFrame(unsigned w, unsigned h, int flip);
void PLAT_HWR_restoreFrontendState(void);
void* PLAT_HWR_getProcAddress(const char* sym);
int PLAT_HWR_maxTextureSize(void);
#endif
