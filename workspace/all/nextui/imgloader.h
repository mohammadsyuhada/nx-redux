#ifndef IMGLOADER_H
#define IMGLOADER_H

#include "sdl.h"
#include <stdbool.h>

// Animation direction enum
enum {
	ANIM_NONE = 0,
	SLIDE_LEFT = 1,
	SLIDE_RIGHT = 2,
	SLIDE_DOWN = 3,
	SLIDE_UP = 4,
};

// Background loading callback
typedef void (*BackgroundLoadedCallback)(SDL_Surface* surface);

// Screen surface (owned by nextui.c)
extern SDL_Surface* screen;

// Shared surfaces (owned by imgloader.c)
extern SDL_Surface* folderbgbmp;
extern SDL_Surface* thumbbmp;

// Synchronization primitives (owned by imgloader.c)
extern SDL_mutex* bgMutex;	  // protects: folderbgbmp, folderbgchanged
extern SDL_mutex* thumbMutex; // protects: thumbbmp, thumbchanged

// Shared state flags (see mutex comments above for which mutex protects each)
extern int folderbgchanged;
extern int thumbchanged;

// Atomic state accessors
void setNeedDraw(int v);
int getNeedDraw(void);

// Lifecycle
void initImageLoaderPool(void);
void cleanupImageLoaderPool(void);

// Background loading
void startLoadFolderBackground(const char* imagePath, BackgroundLoadedCallback callback);
void onBackgroundLoaded(SDL_Surface* surface);

// Thumbnail loading
bool startLoadThumb(const char* thumbpath);
// Drop any cached thumbnail (or cached miss) for `path` so the next
// startLoadThumb reads it fresh from disk. Safe if not present.
void thumbCacheInvalidate(const char* path);
int thumbCheckAsyncLoaded(void);

// Layer render helpers shared by the main render loop (moved from nextui.c)
void updateBackgroundLayer(SDL_Surface* blackBG);
void renderThumbnail(int reset_changed, bool hide);

#endif // IMGLOADER_H
