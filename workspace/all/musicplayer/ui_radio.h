#ifndef __UI_RADIO_H__
#define __UI_RADIO_H__

#include "api.h"
#include "ui_listview.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

// GPU layer for buffer indicator
#define LAYER_BUFFER 4

// Full-mode ListViews: ui_radio.c owns/renders them, module_radio.c drives
// their input through these accessors.
ListView* RadioList_view(void);
ListView* RadioCountries_view(void);

// Render the radio station list
void render_radio_list(SDL_Surface* screen, IndicatorType show_setting,
					   const char* toast_message, uint32_t toast_time);

// Render the radio playing screen
void render_radio_playing(SDL_Surface* screen, IndicatorType show_setting, int radio_selected);

// Render add stations - country selection screen
void render_radio_add(SDL_Surface* screen, IndicatorType show_setting,
					  const char* toast_message, uint32_t toast_time);

// Render add stations - station selection screen
void render_radio_add_stations(SDL_Surface* screen, IndicatorType show_setting,
							   const char* country_code,
							   int add_station_selected, int* add_station_scroll,
							   const int* sorted_indices, int sorted_count,
							   const char* toast_message, uint32_t toast_time);

// Render help/instructions screen
void render_radio_help(SDL_Surface* screen, IndicatorType show_setting, int* help_scroll);

// GPU buffer indicator and status functions (rendered independently like Spectrum/PlayTime)
void RadioStatus_setPosition(int bar_x, int bar_y, int bar_w, int bar_h,
							 int left_x, int left_y);
void RadioStatus_clear(void);
bool RadioStatus_needsRefresh(void);
void RadioStatus_renderGPU(void);

#endif
