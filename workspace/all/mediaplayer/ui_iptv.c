#include <stdio.h>
#include <string.h>

#include "ui_list.h"
#include "api.h"
#include "ui_buttonhintbar.h"
#include "ui_menubar.h"
#include "ui_iptv.h"
#include "ui_fonts.h"
#include "ui_listview.h"
#include "ui_toast.h"
#include "iptv.h"
#include "iptv_curated.h"

// Full-mode ListViews (the widget owns selection, scroll, glide and marquee;
// module_iptv drives input through the accessors below).
static ListView iptv_user_channels_view;
static ListView iptv_curated_countries_view;

ListView* IPTVUserChannels_view(void) {
	return &iptv_user_channels_view;
}
ListView* IPTVCuratedCountries_view(void) {
	return &iptv_curated_countries_view;
}

static void iptv_user_get_row(void* ctx, int i, bool selected,
							  ListViewRow* out) {
	const IPTVChannel* channels = ctx;
	out->label = channels[i].name;
	(void)selected;
}

static void iptv_countries_get_row(void* ctx, int i, bool selected,
								   ListViewRow* out) {
	// Channel counts are unknown until a country is fetched (lazy per-country
	// loading), so country rows show just the name.
	const CuratedTVCountry* countries = ctx;
	out->label = countries[i].name;
	(void)selected;
}

// Render user's channel list (main screen). The widget absorbs the empty
// state when count == 0.
void render_iptv_user_channels(SDL_Surface* screen, IndicatorType show_setting,
							   const char* toast_message, uint32_t toast_time) {
	(void)show_setting;
	GFX_clear(screen);

	UI_renderMenuBar(screen, "Online TV");

	int channel_count = IPTV_getUserChannelCount();
	const IPTVChannel* channels = IPTV_getUserChannels();

	ListView* v = &iptv_user_channels_view;
	v->title = NULL; // menu bar drawn above (caller-owned chrome)
	v->font = font.medium;
	v->count = channel_count;
	v->get_row = iptv_user_get_row;
	v->ctx = (void*)channels;
	v->list_id = (const void*)channels;
	v->empty_title = "No channels saved";
	v->empty_subtitle = "Press A to browse channels";
	// Empty state carries its own centered buttons; the bottom hint bar only
	// appears with content. Browse/remove otherwise live in the MENU menu.
	v->empty_btn_pairs = (char*[]){"B", "BACK", "A", "MANAGE", NULL};
	v->hint_pairs = (channel_count > 0)
						? (char*[]){"B", "BACK", "A", "PLAY", NULL}
						: NULL;
	UI_listViewRender(v, screen);

	// Playback failure / status feedback set by module_iptv.
	UI_renderToast(screen, toast_message, toast_time);
}

// Render curated country list for browsing
void render_iptv_curated_countries(SDL_Surface* screen, IndicatorType show_setting,
								   const char* toast_message, uint32_t toast_time) {
	(void)show_setting;
	GFX_clear(screen);

	UI_renderMenuBar(screen, "Browse Channels");

	int country_count = IPTV_curated_get_country_count();
	const CuratedTVCountry* countries = IPTV_curated_get_countries();

	ListView* v = &iptv_curated_countries_view;
	v->title = NULL; // menu bar drawn above (caller-owned chrome)
	v->font = font.medium;
	v->count = country_count;
	v->get_row = iptv_countries_get_row;
	v->ctx = (void*)countries;
	v->list_id = (const void*)countries;
	// Catalog is fetched from iptv-org; an empty list means the fetch failed.
	v->empty_title = "Couldn't load channel list";
	v->empty_subtitle = "Press MENU to refresh, or check Wi-Fi";
	v->hint_pairs = (char*[]){"B", "BACK", "A", "SELECT", NULL};
	UI_listViewRender(v, screen);

	// Disclaimer / error feedback set by module_iptv.
	UI_renderToast(screen, toast_message, toast_time);
}

// Render curated channels for a country
void render_iptv_curated_channels(SDL_Surface* screen, IndicatorType show_setting,
								  const char* country_code,
								  int selected, int* scroll_offset,
								  const int* sorted_indices, int sorted_count,
								  const char* toast_message, uint32_t toast_time) {
	GFX_clear(screen);

	int hw = screen->w;
	char truncated[256];

	// Get country name for title
	const char* country_name = "Channels";
	const CuratedTVCountry* countries = IPTV_curated_get_countries();
	int country_count = IPTV_curated_get_country_count();
	for (int i = 0; i < country_count; i++) {
		if (strcmp(countries[i].code, country_code) == 0) {
			country_name = countries[i].name;
			break;
		}
	}

	UI_renderMenuBar(screen, country_name);

	int channel_count = 0;
	const CuratedTVChannel* channels = IPTV_curated_get_channels(country_code, &channel_count);

	ListLayout layout = UI_calcListLayout(screen);
	UI_adjustListScroll(selected, scroll_offset, layout.items_per_page);

	// Determine if the currently selected channel is already added
	bool selected_exists = false;
	if (sorted_count > 0 && selected < sorted_count) {
		int sel_actual = sorted_indices[selected];
		if (sel_actual < channel_count) {
			selected_exists = IPTV_userChannelExists(channels[sel_actual].url);
		}
	}

	for (int i = 0; i < layout.items_per_page && *scroll_offset + i < sorted_count; i++) {
		int idx = *scroll_offset + i;
		int actual_idx = sorted_indices[idx];
		const CuratedTVChannel* channel = &channels[actual_idx];
		bool is_selected = (idx == selected);
		bool added = IPTV_userChannelExists(channel->url);

		int y = layout.list_y + i * layout.item_h;

		// Calculate prefix width for added indicator
		int prefix_width = 0;
		if (added) {
			int pw, ph;
			GFX_measureText(font.small, "[+]", &pw, &ph);
			prefix_width = pw + SCALE1(6);
		}

		// Render pill background and get text position
		int name_max_width = layout.max_width - prefix_width - SCALE1(60);
		int text_width = GFX_truncateText(font.medium, channel->name, truncated, name_max_width, SCALE1(BUTTON_PADDING * 2));
		int pill_width = MIN(layout.max_width, prefix_width + text_width + SCALE1(BUTTON_PADDING));

		SDL_Rect pill_rect = {SCALE1(PADDING), y, pill_width, layout.item_h};
		Fonts_drawListItemBg(screen, &pill_rect, is_selected);

		int text_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
		int text_y = y + (layout.item_h - TTF_FontHeight(font.medium)) / 2;

		// Added indicator prefix
		if (added) {
			SDL_Color prefix_color = Fonts_getListTextColor(is_selected);
			SDL_Surface* prefix_text = GFX_renderText(font.small, "[+]", prefix_color);
			if (prefix_text) {
				SDL_BlitSurface(prefix_text, NULL, screen, &(SDL_Rect){text_x, y + (layout.item_h - prefix_text->h) / 2});
				SDL_FreeSurface(prefix_text);
			}
		}

		// Channel name
		UI_renderListItemText(screen, NULL, channel->name, font.medium,
							  text_x + prefix_width, text_y, name_max_width, is_selected);

		// Category on right
		if (channel->category[0]) {
			SDL_Color cat_color = is_selected ? COLOR_GRAY : COLOR_DARK_TEXT;
			SDL_Surface* cat_text = GFX_renderText(font.tiny, channel->category, cat_color);
			if (cat_text) {
				SDL_BlitSurface(cat_text, NULL, screen, &(SDL_Rect){hw - cat_text->w - SCALE1(PADDING * 2), y + (layout.item_h - cat_text->h) / 2});
				SDL_FreeSurface(cat_text);
			}
		}
	}

	UI_renderScrollIndicators(screen, *scroll_offset, layout.items_per_page, sorted_count);

	// Toast notification
	UI_renderToast(screen, toast_message, toast_time);

	// Button hints - dynamic based on whether selected channel is already added
	if (selected_exists) {
		UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "A", "REMOVE", NULL});
	} else {
		UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "A", "ADD", NULL});
	}
}
