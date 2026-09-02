#ifndef __WIFI_H__
#define __WIFI_H__

#include "api.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

// Returns true if WiFi is already connected. Never enables or connects WiFi.
// When scr is non-NULL and not connected, shows a brief status message
// ("WiFi is off / not connected — enable in Settings") and returns false.
// scr=NULL (background threads) just returns the connected state with no UI.
bool Wifi_ensureConnected(SDL_Surface* scr, IndicatorType show_setting);

// Check if WiFi is currently connected
bool Wifi_isConnected(void);

// Register an app hook run before the WiFi status message render, replacing the
// default scroll-layer clear (e.g. to also reset app-side scroll state)
void Wifi_setConnectScreenHook(void (*hook)(void));

#endif
