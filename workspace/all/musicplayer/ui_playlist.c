#include <stdio.h>

#include "defines.h"
#include "api.h"
#include "ui_menubar.h"
#include "ui_playlist.h"
#include "ui_icons.h"
#include "ui_listview.h"

// Full-mode ListViews (the widget owns selection, scroll, glide and marquee;
// module_playlist drives input through the accessors below).
static ListView playlist_list_view;
static ListView playlist_detail_view;

ListView* PlaylistList_view(void) {
	return &playlist_list_view;
}
ListView* PlaylistDetail_view(void) {
	return &playlist_detail_view;
}

// "%s (%d)" scratch: the widget consumes the label before the next get_row
// call, so one static buffer per provider is enough.
static char playlist_row_buf[256];

static void playlist_list_get_row(void* ctx, int i, bool selected,
								  ListViewRow* out) {
	PlaylistInfo* pls = ctx;
	snprintf(playlist_row_buf, sizeof(playlist_row_buf), "%s (%d)",
			 pls[i].name, pls[i].track_count);
	out->label = playlist_row_buf;
	(void)selected;
}

// Row provider for the tracks list: icon variant follows the widget-supplied
// `selected` flag (the glide-tracking row_sel), preserving the
// pill-follows-icon behavior.
static void playlist_detail_get_row(void* ctx, int i, bool selected,
									ListViewRow* out) {
	PlaylistTrack* tracks = ctx;
	out->label = tracks[i].name;
	out->icon = Icons_isLoaded() ? Icons_getForFormat(tracks[i].format, selected) : NULL;
}

void render_playlist_list(SDL_Surface* screen, IndicatorType show_setting,
						  PlaylistInfo* playlists, int count) {
	(void)show_setting;
	GFX_clear(screen);

	UI_renderMenuBar(screen, "Playlists");

	ListView* v = &playlist_list_view;
	v->title = NULL; // menu bar drawn above (caller-owned chrome)
	v->font = font.medium;
	v->count = count;
	v->get_row = playlist_list_get_row;
	v->ctx = playlists;
	v->list_id = (const void*)playlists;
	v->empty_title = "No playlists saved";
	v->empty_subtitle = "Press A to create a playlist";
	v->empty_y_label = NULL;
	// Empty state carries all hints; the bottom bar only appears with content
	v->empty_btn_pairs = (char*[]){"B", "BACK", "A", "NEW", NULL};
	v->hint_pairs = (count > 0)
						? (char*[]){"B", "BACK", "A", "SELECT", NULL}
						: NULL;
	UI_listViewRender(v, screen);
}

void render_playlist_detail(SDL_Surface* screen, IndicatorType show_setting,
							const char* playlist_name,
							PlaylistTrack* tracks, int count) {
	(void)show_setting;
	GFX_clear(screen);

	char title[300];
	snprintf(title, sizeof(title), "Playlist %s", playlist_name);
	UI_renderMenuBar(screen, title);

	ListView* v = &playlist_detail_view;
	v->title = NULL; // menu bar drawn above (caller-owned chrome)
	v->font = font.medium;
	v->count = count;
	v->get_row = playlist_detail_get_row;
	v->ctx = tracks;
	// Identity: the playlist's name pointer, NOT the tracks array — the
	// module refills one static tracks buffer in place for every playlist,
	// so the array pointer never changes. The name pointer differs per
	// playlist entry, making a playlist switch snap instead of glide.
	v->list_id = (const void*)playlist_name;
	v->empty_title = "No tracks in playlist";
	v->empty_subtitle = "Add tracks from the music browser";
	v->empty_y_label = NULL;
	// Empty state carries all hints; the bottom bar only appears with content
	v->empty_btn_pairs = (char*[]){"B", "BACK", NULL};
	v->hint_pairs = (count > 0)
						? (char*[]){"B", "BACK", "A", "PLAY", NULL}
						: NULL;
	UI_listViewRender(v, screen);
}
