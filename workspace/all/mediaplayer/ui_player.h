#ifndef __UI_PLAYER_H__
#define __UI_PLAYER_H__
#include <SDL2/SDL.h>
#include "api.h"
#include "video_browser.h"
#include "ui_list.h"

// Render the video file browser
void render_video_browser(SDL_Surface* screen, IndicatorType show_setting,
						  VideoBrowserContext* ctx, ScrollTextState* scroll,
						  int selected_resume_sec);

#endif
