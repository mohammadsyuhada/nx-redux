// area_scale.h — alpha-aware area-average resampling of ARGB8888 pixels. SDL-free.
//
// SDL_BlitScaled on software surfaces is nearest-neighbour: shrinking a logo picks one source pixel per output pixel
// and skips the rest, so its anti-aliased edges turn into stair-steps. Here every output pixel averages all the source
// pixels it covers (fractional edges weighted by overlap), with the colour weighted by alpha so the transparent
// pixels around a logo don't darken its edges.
#ifndef AREA_SCALE_H
#define AREA_SCALE_H

#include <stdint.h>

// Resample src (sw×sh, `spitch` pixels per row, straight alpha) into dst (dw×dh, `dpitch` pixels per row). Meant for
// shrinking; growing works too (each output pixel blends the one or two source pixels under it). Returns 0, or -1 on
// bad sizes or no memory.
int AreaScale_argb(const uint32_t* src, int sw, int sh, int spitch, uint32_t* dst, int dw, int dh, int dpitch);

#endif
