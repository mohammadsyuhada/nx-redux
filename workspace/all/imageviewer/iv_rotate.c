#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#include "iv_rotate.h"

#ifndef FFMPEG_PATH
#define FFMPEG_PATH "/usr/bin/ffmpeg"
#endif

SDL_Surface* IvRotate_surface(SDL_Surface* src, int turns) {
	turns = ((turns % 4) + 4) % 4;
	int bpp = src->format->BytesPerPixel;
	if (turns == 0 || bpp < 2)
		return NULL;

	int dw = turns == 2 ? src->w : src->h;
	int dh = turns == 2 ? src->h : src->w;
	SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, src->format->BitsPerPixel,
													  src->format->format);
	if (!dst)
		return NULL;

	SDL_LockSurface(src);
	SDL_LockSurface(dst);
	const Uint8* sp = src->pixels;
	Uint8* dp = dst->pixels;
	for (int y = 0; y < src->h; y++) {
		const Uint8* row = sp + y * src->pitch;
		for (int x = 0; x < src->w; x++) {
			int nx, ny;
			if (turns == 1) { // clockwise: top row becomes the right column
				nx = src->h - 1 - y;
				ny = x;
			} else if (turns == 2) {
				nx = src->w - 1 - x;
				ny = src->h - 1 - y;
			} else {
				nx = y;
				ny = src->w - 1 - x;
			}
			memcpy(dp + ny * dst->pitch + nx * bpp, row + x * bpp, bpp);
		}
	}
	SDL_UnlockSurface(dst);
	SDL_UnlockSurface(src);
	return dst;
}

static bool rotate_png(const char* path, const char* tmp, int turns) {
	SDL_Surface* raw = IMG_Load(path);
	if (!raw)
		return false;
	// 32-bit with alpha so palettized/grey PNGs rotate and keep transparency.
	SDL_Surface* argb = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!argb)
		return false;
	SDL_Surface* rotated = IvRotate_surface(argb, turns);
	SDL_FreeSurface(argb);
	if (!rotated)
		return false;
	bool ok = IMG_SavePNG(rotated, tmp) == 0;
	SDL_FreeSurface(rotated);
	return ok;
}

static bool rotate_ffmpeg(const char* path, const char* tmp, int turns, bool is_jpg) {
	const char* vf = turns == 1 ? "transpose=1" : turns == 2 ? "transpose=1,transpose=1"
															 : "transpose=2";
	pid_t pid = fork();
	if (pid < 0)
		return false;
	if (pid == 0) {
		freopen("/dev/null", "r", stdin);
		freopen("/dev/null", "w", stdout);
		freopen("/dev/null", "w", stderr);
		// GIF keeps every frame; the still formats write exactly one.
		if (is_jpg)
			execl(FFMPEG_PATH, "ffmpeg", "-nostdin", "-i", path, "-vf", vf,
				  "-frames:v", "1", "-c:v", "mjpeg", "-q:v", "2", "-y", tmp, (char*)NULL);
		else
			execl(FFMPEG_PATH, "ffmpeg", "-nostdin", "-i", path, "-vf", vf,
				  "-y", tmp, (char*)NULL);
		_exit(1);
	}
	int status = 0;
	if (waitpid(pid, &status, 0) < 0)
		return false;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 && access(tmp, F_OK) == 0;
}

bool IvRotate_file(const char* path, int turns) {
	turns = ((turns % 4) + 4) % 4;
	if (turns == 0)
		return true;

	const char* slash = strrchr(path, '/');
	const char* dot = strrchr(path, '.');
	if (!dot || (slash && dot < slash))
		return false;
	const char* ext = dot + 1;

	// Dot-prefixed so the browser never lists it; same extension so ffmpeg
	// picks the matching muxer.
	char tmp[600];
	if (slash)
		snprintf(tmp, sizeof(tmp), "%.*s.rotate-tmp.%s", (int)(slash - path + 1), path, ext);
	else
		snprintf(tmp, sizeof(tmp), ".rotate-tmp.%s", ext);
	unlink(tmp);

	bool ok;
	if (strcasecmp(ext, "png") == 0)
		ok = rotate_png(path, tmp, turns);
	else
		ok = rotate_ffmpeg(path, tmp, turns,
						   strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0);

	if (ok && rename(tmp, path) == 0) {
		sync();
		return true;
	}
	unlink(tmp);
	return false;
}
