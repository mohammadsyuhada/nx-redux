#include "drm_scanout.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#if defined(__has_include)
#if __has_include(<drm/drm.h>) && __has_include(<drm/drm_mode.h>)
#define HAVE_DRM 1
#include <drm/drm.h>
#include <drm/drm_mode.h>
// Toolchain UAPI headers predate kernel 5.7; the tg5050 kernel (5.15) has it.
#ifndef DRM_IOCTL_MODE_GETFB2
#define DRM_IOCTL_MODE_GETFB2 DRM_IOWR(0xCE, struct drm_mode_fb_cmd2)
#endif
#endif
#endif

#ifdef HAVE_DRM

static int drm_ioctl(int fd, unsigned long req, void* arg) {
	int ret;
	do {
		ret = ioctl(fd, req, arg);
	} while (ret < 0 && (errno == EINTR || errno == EAGAIN));
	return ret;
}

int drm_scanout_read(drm_scanout_info* info, uint8_t* buf, size_t buf_size) {
	int ok = 0;
	int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return 0;

	struct drm_set_client_cap cap = {DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1};
	drm_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);

	uint32_t plane_ids[64];
	struct drm_mode_get_plane_res pres = {0};
	pres.plane_id_ptr = (uintptr_t)plane_ids;
	pres.count_planes = 64;
	if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) ||
		pres.count_planes == 0 || pres.count_planes > 64) {
		close(fd);
		return 0;
	}

	for (uint32_t i = 0; i < pres.count_planes && !ok; i++) {
		struct drm_mode_get_plane pl = {0};
		pl.plane_id = plane_ids[i];
		if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &pl) || !pl.fb_id)
			continue;

		struct drm_mode_fb_cmd2 fb = {0};
		fb.fb_id = pl.fb_id;
		if (drm_ioctl(fd, DRM_IOCTL_MODE_GETFB2, &fb))
			continue;
		if (fb.modifier[0] != 0) // tiled/compressed (e.g. AFBC): can't read raw
			continue;

		// Single-plane 32bpp RGB only; video overlays (NV12 etc.) are skipped
		// and the loop falls through to the UI plane.
		const char* fmt = NULL;
		switch (fb.pixel_format) {
		case 0x34325258: // XR24 XRGB8888: B G R X in memory
		case 0x34325241:
			fmt = "bgr0";
			break;		 // AR24 ARGB8888
		case 0x34325842: // XB24 XBGR8888: R G B X in memory
		case 0x34324241:
			fmt = "rgb0";
			break; // AB24 ABGR8888
		default:
			continue;
		}

		size_t row = (size_t)fb.width * 4;
		if (buf && buf_size < row * fb.height)
			continue; // caller's buffer can't hold this frame

		if (buf) {
			struct drm_prime_handle prime = {0};
			prime.handle = fb.handles[0];
			prime.flags = O_CLOEXEC;
			if (drm_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime))
				continue;

			size_t len = (size_t)fb.offsets[0] + (size_t)fb.pitches[0] * fb.height;
			uint8_t* map = mmap(NULL, len, PROT_READ, MAP_SHARED, prime.fd, 0);
			if (map != MAP_FAILED) {
				const uint8_t* src = map + fb.offsets[0];
				for (uint32_t y = 0; y < fb.height; y++)
					memcpy(buf + y * row, src + (size_t)y * fb.pitches[0], row);
				munmap(map, len);
				ok = 1;
			}
			close(prime.fd);
		} else {
			ok = 1; // probe only
		}

		if (ok) {
			info->width = fb.width;
			info->height = fb.height;
			snprintf(info->pixfmt, sizeof(info->pixfmt), "%s", fmt);
		}
	}
	close(fd); // releases the GEM handle references from GETFB2
	return ok;
}

#else

int drm_scanout_read(drm_scanout_info* info, uint8_t* buf, size_t buf_size) {
	(void)info;
	(void)buf;
	(void)buf_size;
	return 0;
}

#endif
