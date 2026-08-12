#ifndef __UI_PLAYER_H__
#define __UI_PLAYER_H__
#include <SDL2/SDL.h>
#include "api.h"
#include "video_browser.h"
#include "ui_list.h"
#include "ui_listview.h"

// Render the video file browser
void render_video_browser(SDL_Surface* screen, IndicatorType show_setting,
						  VideoBrowserContext* ctx);

// Full-mode ListView for the video browser: ui_player.c owns/renders it,
// module_player.c drives input through this accessor.
ListView* VideoBrowser_view(void);

#endif
