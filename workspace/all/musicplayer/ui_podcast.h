#ifndef MUSIC_UI_PODCAST_H
#define MUSIC_UI_PODCAST_H

#include "../common/sdl.h"
#include <stdbool.h>

typedef int MusicIndicatorType;
typedef enum {
	PODCAST_MANAGE_SEARCH = 0,
	PODCAST_MANAGE_TOP_SHOWS,
	PODCAST_MANAGE_COUNT
} PodcastManageMenuItem;

void render_podcast_main_page(SDL_Surface* screen, MusicIndicatorType show_setting,
							  int selected, int* scroll,
							  const char* toast_message, uint32_t toast_time);
void Podcast_clearThumbnailCache(void);
bool UIPodcast_artworkBusy(void);
bool Podcast_loadPendingThumbnails(void);
void render_podcast_manage(SDL_Surface* screen, MusicIndicatorType show_setting,
						   int menu_selected, int subscription_count);
void render_podcast_top_shows(SDL_Surface* screen, MusicIndicatorType show_setting,
							  int selected, int* scroll,
							  const char* toast_message, uint32_t toast_time);
void render_podcast_search_results(SDL_Surface* screen, MusicIndicatorType show_setting,
								   int selected, int* scroll,
								   const char* toast_message, uint32_t toast_time);
void render_podcast_episodes(SDL_Surface* screen, MusicIndicatorType show_setting,
							 int feed_index, int selected, int* scroll,
							 const char* toast_message, uint32_t toast_time);
void render_podcast_download_queue(SDL_Surface* screen, MusicIndicatorType show_setting,
								   int selected, int* scroll,
								   const char* toast_message, uint32_t toast_time);
void render_podcast_playing(SDL_Surface* screen, MusicIndicatorType show_setting,
							int feed_index, int episode_index);
void render_podcast_loading(SDL_Surface* screen, const char* message);
bool Podcast_isTitleScrolling(void);
bool Podcast_titleScrollNeedsRender(void);
void Podcast_animateTitleScroll(void);
void Podcast_clearTitleScroll(void);
void Podcast_clearArtwork(void);

#define LAYER_PODCAST_PROGRESS 3
void PodcastProgress_setPosition(int bar_x, int bar_y, int bar_w, int bar_h,
								 int time_y, int screen_w, int duration_ms);
void PodcastProgress_clear(void);
bool PodcastProgress_needsRefresh(void);
void PodcastProgress_renderGPU(void);

#endif
