#ifndef HWR_PLAT_H
#define HWR_PLAT_H
// Platform side of minarch's libretro GPU-render path (generic_video.c).
// Plain C types only so the libretro-side module stays host-testable.
unsigned PLAT_HWR_create(unsigned w, unsigned h, int depth, int stencil);
void PLAT_HWR_destroy(void);
int PLAT_HWR_resize(unsigned w, unsigned h); // in place, same FBO name; 1 when complete
void PLAT_HWR_makeCurrent(void);
void PLAT_HWR_setFrame(unsigned w, unsigned h, int flip);
void PLAT_HWR_restoreFrontendState(void);
// Right before each core frame: leave the default vertex array and no array
// buffer bound, the state a core expects to start from.
void PLAT_HWR_prepareCoreFrame(void);
void* PLAT_HWR_getProcAddress(const char* sym);
int PLAT_HWR_maxTextureSize(void);
// Debug HUD over GPU frames: an RGBA buffer the size of the frame, drawn over
// the game with alpha; NULL hides it.
void PLAT_HWR_setHud(const void* rgba, int w, int h);
// Average colour source for ambient LEDs: the frame downsampled on the GPU to
// w x h RGBA (w*h*4 bytes). Returns 0 when there is no frame yet.
int PLAT_HWR_readAverage(void* rgba, int w, int h);
#endif
