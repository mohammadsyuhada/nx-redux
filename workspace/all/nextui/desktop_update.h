#ifndef DESKTOP_UPDATE_H
#define DESKTOP_UPDATE_H
#include "sdl.h"
// Desktop-only OTA: startCheck spawns a background version check;
// offerIfReady shows a one-shot confirm dialog once the check lands.
// Both are no-ops on device builds.
void DesktopUpdate_startCheck(void);
void DesktopUpdate_offerIfReady(SDL_Surface* screen);
#endif
