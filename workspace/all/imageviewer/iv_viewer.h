#ifndef IV_VIEWER_H
#define IV_VIEWER_H
#include <stdbool.h>
#include "sdl.h"
#include "iv_browser.h"
#include "iv_loader.h"

typedef enum { IV_VIEWER_CONTINUE,
			   IV_VIEWER_EXIT,
			   IV_VIEWER_ERROR } IvViewerStatus;

// Begin viewing browser->entries[entry_index] (must be a file). Requests the
// FULL decode; the viewer renders a loading state until it arrives.
void IvViewer_open(ImageBrowserContext* browser, int entry_index);
// Handle input + drain loader results. ERROR = initial image failed to load
// (caller returns to browser and toasts IvViewer_lastError()).
IvViewerStatus IvViewer_update(SDL_Surface* screen, bool* dirty);
void IvViewer_render(SDL_Surface* screen);
IvLoadStatus IvViewer_lastError(void);
// True while a load/prefetch is outstanding (caller keeps the loop hot).
bool IvViewer_busy(void);
// Free current image + prefetch cache (call on exit from viewer).
void IvViewer_close(void);
#endif
