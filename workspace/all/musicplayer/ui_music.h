#ifndef __UI_MUSIC_H__
#define __UI_MUSIC_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "api.h"
#include "browser.h"
#include "ui_listview.h"

// Use LAYER_THUMBNAIL (3) for playtime - platform only supports layers 0-5
#define LAYER_PLAYTIME 3
#define LAYER_LYRICS 2

// Render the file browser screen
void render_browser(SDL_Surface* screen, IndicatorType show_setting, BrowserContext* browser);

// Render the now playing screen
// playlist_track_num and playlist_total: if > 0, use these instead of browser counts
void render_playing(SDL_Surface* screen, IndicatorType show_setting, BrowserContext* browser,
					bool shuffle_enabled, bool repeat_enabled,
					int playlist_track_num, int playlist_total);

// The browser's full-mode ListView (owns selection/scroll/glide/marquee)
ListView* MusicBrowser_view(void);

// Check if player title has active scrolling (for refresh optimization)
bool player_needs_scroll_refresh(void);

// Check if player title scroll needs a render to transition (delay phase)
bool player_title_scroll_needs_render(void);

// Animate player title scroll (GPU mode, no screen redraw needed)
void player_animate_scroll(void);

// Playtime GPU rendering functions
void PlayTime_setPosition(int x, int y, int duration_x);
void PlayTime_renderGPU(void);
bool PlayTime_needsRefresh(void);
void PlayTime_clear(void);

// Lyrics GPU rendering functions
// Where the lyrics go: from (x, y), max_w wide (one truncated row a line) or,
// on devices that wrap lyrics (MUSIC_LYRICS_WRAP_W), down to max_h tall.
void Lyrics_setGPUPosition(int x, int y, int max_w, int max_h);
void Lyrics_renderGPU(void);
bool Lyrics_GPUneedsRefresh(void);
void Lyrics_clearGPU(void);

#endif
