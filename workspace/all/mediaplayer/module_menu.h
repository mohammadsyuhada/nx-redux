#ifndef __MODULE_MENU_H__
#define __MODULE_MENU_H__

#include <SDL2/SDL.h>

#define MENU_LOCAL 0
#define MENU_IPTV 1
#define MENU_QUIT -1

int MenuModule_run(SDL_Surface* screen);
void MenuModule_setToast(const char* message);

#endif
