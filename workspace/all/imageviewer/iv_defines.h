#ifndef IV_DEFINES_H
#define IV_DEFINES_H

#include "defines.h" // SDCARD_PATH, SCALE1, MAX_PATH, colors, fonts

#define IMAGES_ROOT SDCARD_PATH "/Images"

// Reject before decode: oversized files and decode bombs (40 MP ≈ 160 MB ARGB)
#define IV_MAX_FILE_BYTES (32 * 1024 * 1024)
#define IV_MAX_PIXELS (40 * 1000 * 1000)

// Prefetch is speculative (nobody has asked to view this neighbor yet), so
// cap it well below the on-demand IV_MAX_PIXELS limit - a worst-case ~40 MP
// neighbor would otherwise burn ~160 MB decoding something that may never
// actually be viewed.
#define IV_PREFETCH_MAX_PIXELS (16 * 1000 * 1000)

#endif
