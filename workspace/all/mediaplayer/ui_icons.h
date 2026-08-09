#ifndef __UI_ICONS_H__
#define __UI_ICONS_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "vp_defines.h"

void Icons_init(void);
void Icons_quit(void);
SDL_Surface* Icons_getFolder(bool selected);
SDL_Surface* Icons_getForFormat(VideoFormat format, bool selected);
bool Icons_isLoaded(void);

#endif
