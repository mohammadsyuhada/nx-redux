#ifndef IV_LOADER_H
#define IV_LOADER_H
#include <stdbool.h>
#include "sdl.h"

typedef enum { IV_LOAD_PREVIEW,
			   IV_LOAD_FULL,
			   IV_LOAD_PREFETCH,
			   IV_PURPOSE_COUNT } IvLoadPurpose;
typedef enum { IV_LOAD_OK,
			   IV_LOAD_ERR_TOO_LARGE,
			   IV_LOAD_ERR_DECODE } IvLoadStatus;

typedef struct {
	char path[512];
	IvLoadPurpose purpose;
	IvLoadStatus status;
	SDL_Surface* surface; // screen format; PREVIEW downscaled to pane box; NULL on error
	int orig_w, orig_h;	  // decode dimensions before any downscale (0 when unknown)
} IvLoadResult;

// preview_max_w/h: pane box the PREVIEW results are downscaled to fit.
void IvLoader_init(SDL_Surface* screen, int preview_max_w, int preview_max_h);
void IvLoader_quit(void);
// Latest-wins per purpose: replaces any not-yet-started request with the same purpose.
void IvLoader_request(const char* path, IvLoadPurpose purpose);
// Drain one finished result per call; false = none ready. Caller owns out->surface.
bool IvLoader_poll(IvLoadResult* out);
#endif
