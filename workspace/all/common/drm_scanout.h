// Read the framebuffer currently scanned out by the display engine straight
// from DRM/KMS: GETFB2 + PRIME export + mmap. Needs only root
// (CAP_SYS_ADMIN), not DRM master, so it captures ANY app — including
// third-party paks that don't publish the GPU mirror. Works on tg5050
// (sunxi-drm, linear XR24 scanout); on tg5040 the DRM node is just the Mali
// render device with no KMS planes, so calls fail fast and the caller's
// fallback source takes over.
//
// Standalone: libc + kernel DRM UAPI headers only (no api.h/defines.h), so
// the capture daemons can link it without pulling in the app stack.
#ifndef DRM_SCANOUT_H
#define DRM_SCANOUT_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
	uint32_t width;
	uint32_t height;
	char pixfmt[8]; // ffmpeg rawvideo pixel_format name: "bgr0" or "rgb0"
} drm_scanout_info;

// Reads the current scanout into buf as packed rows (width*4*height bytes),
// filling info with the geometry/format. buf may be NULL to probe geometry
// only. buf_size guards against a mode change growing the frame mid-use.
// Rows are top-down (no vflip needed at encode). Returns 1 on success.
int drm_scanout_read(drm_scanout_info* info, uint8_t* buf, size_t buf_size);

#endif
