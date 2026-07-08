#ifndef __WIFI_H__
#define __WIFI_H__

#include "api.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

// Ensure WiFi is connected, enabling if necessary
// Returns true if connected, false otherwise
// Shows "Connecting..." screen while waiting (if scr is not NULL)
// Can be called from background threads with scr=NULL to skip UI rendering
bool Wifi_ensureConnected(SDL_Surface* scr, IndicatorType show_setting);

// Check if WiFi is currently connected
bool Wifi_isConnected(void);

// Register an app hook run before each "Connecting..." render, replacing the
// default scroll-layer clear (e.g. to also reset app-side scroll state)
void Wifi_setConnectScreenHook(void (*hook)(void));

#endif
