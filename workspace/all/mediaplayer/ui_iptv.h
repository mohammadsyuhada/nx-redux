#ifndef __UI_IPTV_H__
#define __UI_IPTV_H__

#include <SDL2/SDL.h>
#include "api.h"
#include "ui_list.h"
#include "ui_listview.h"

// Full-mode ListViews: ui_iptv.c owns/renders them, module_iptv.c drives
// their input through these accessors.
ListView* IPTVUserChannels_view(void);
ListView* IPTVCuratedCountries_view(void);

// Render user's channel list (main screen). The widget absorbs the empty
// state (count == 0), so there is no separate empty-state renderer.
void render_iptv_user_channels(SDL_Surface* screen, IndicatorType show_setting,
							   const char* toast_message, uint32_t toast_time);

// Render curated country list for browsing
void render_iptv_curated_countries(SDL_Surface* screen, IndicatorType show_setting,
								   const char* toast_message, uint32_t toast_time);

// Render curated channels for a country
void render_iptv_curated_channels(SDL_Surface* screen, IndicatorType show_setting,
								  const char* country_code,
								  int selected, int* scroll_offset,
								  const int* sorted_indices, int sorted_count,
								  const char* toast_message, uint32_t toast_time);

#endif
