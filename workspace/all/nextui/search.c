#include "search.h"
#include "utils.h"
#include "config.h"
#include "content.h"
#include "defines.h"
#include "display_helper.h"
#include "gamelist.h"
#include "imgloader.h"
#include "launcher.h"
#include "types.h"
#include "ui_message.h"
#include "ui_keyboard.h"
#include "ui_list.h"
#include "ui_listview.h"
#include "api.h"

#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Array* search_results = NULL;
static ListView search_view;

ListView* Search_view(void) {
	return &search_view;
}

static void search_get_row(void* ctx, int i, bool selected, ListViewRow* out) {
	(void)ctx;
	(void)selected;
	Entry* entry = search_results->items[i];
	char* name = entry->name;
	trimSortingMeta(&name);
	out->label = name;
}

void Search_init(void) {
	search_results = NULL;
}

void Search_quit(void) {
	if (search_results) {
		EntryArray_free(search_results);
		search_results = NULL;
	}
}

bool Search_open(void) {
	char* query = UIKeyboard_open("Search");
	// the keyboard cleared every GPU layer; if we end up staying on the game
	// list (cancel / empty query) nothing else re-uploads its background
	requestBackgroundReupload();
	PAD_poll();
	PAD_reset();

	if (!query || strlen(query) == 0) {
		if (query)
			free(query);
		return false;
	}

	// Free previous results
	if (search_results) {
		EntryArray_free(search_results);
		search_results = NULL;
	}

	search_results = Content_searchRoms(query);
	free(query);

	// Snap the pill to the top on every new query: the result Array is
	// rebuilt per query so its pointer identity normally changes, but the
	// free-then-realloc could reuse the same address — reset defensively.
	UI_listViewReset(&search_view, search_results ? search_results->count : 0,
					 search_results);

	return true;
}

SearchResult Search_handleInput(unsigned long now) {
	(void)now;
	SearchResult result = {0};
	result.screen = SCREEN_SEARCH;

	int total = search_results ? search_results->count : 0;

	int prev_selected = search_view.selected;
	ListViewAction act = UI_listViewHandleInput(&search_view);
	if (act.type == LISTVIEW_BACK) {
		result.screen = SCREEN_GAMELIST;
		result.dirty = true;
		result.folderbgchanged = true;
		GFX_clearLayers(LAYER_SCROLLTEXT);
		ScrollText_clear(&search_view.marquee);
	} else if (act.type == LISTVIEW_ACTIVATED) {
		Entry* entry = search_results->items[act.index];
		Entry_open(entry);
		result.startgame = true;
		result.dirty = true;
	}
	// X resumes the selected game's save state; L2/R2 excluded because the
	// combo layer owns those chords (same guard as GameList_handleInput).
	else if (total > 0 && PAD_justReleased(BTN_RESUME) &&
			 !PAD_isPressed(BTN_L2) && !PAD_isPressed(BTN_R2)) {
		Entry* entry = search_results->items[search_view.selected];
		// selection may have moved this same frame; don't trust the resume
		// state the last render probed for the previous entry
		readyResume(entry);
		if (resume.can_resume) {
			resume.should_resume = true;
			Entry_open(entry);
			result.startgame = true;
			result.dirty = true;
		}
	}
	// Y launches netplay-capable ROMs with netplay (same flow as gamelist)
	else if (total > 0 && PAD_justReleased(BTN_Y)) {
		Entry* entry = search_results->items[search_view.selected];
		if (GameList_entryNetplayCapable(entry)) {
			putFile(NETPLAY_LAUNCH_PATH, "1\n");
			Entry_open(entry);
			// disarm if no launch was queued (mirrors gamelist's guard)
			if (!quit)
				unlink(NETPLAY_LAUNCH_PATH);
			result.startgame = true;
			result.dirty = true;
		}
	}
	// Dirty cadence matches the pre-migration wiring: selection travel and
	// the glide drive full renders; the marquee's pre-scroll delay and steady
	// scrolling are served by nextui's idle branch (UI_listViewMarqueeBusy +
	// UI_listViewTickIdle), where the thumbnail layer is change-guarded.
	// UI_listViewBusy must NOT feed dirty here or in nextui.c: its
	// needsRender term extends the dirty burst ~1s past the glide settle,
	// and every one of those frames re-runs Search_render -> startLoadThumb,
	// which re-latches thumbchanged and re-uploads the thumbnail GPU layer -
	// a visible artwork flicker after the pill lands (and a first-activation
	// GFX_resetScrollText path the idle-driven marquee never takes). This
	// result.dirty is the SINGLE dirty source for the search list.
	if (search_view.selected != prev_selected ||
		UI_listGlideActive(&search_view.glide))
		result.dirty = true;
	return result;
}

void Search_render(SDL_Surface* screen, int lastScreen) {
	if (lastScreen != SCREEN_SEARCH) {
		onBackgroundLoaded(NULL);
		GFX_clearLayers(LAYER_THUMBNAIL);
		// we just cleared the shared folder background; make the game list
		// reload it on return instead of trusting its stale change-detection
		GameList_invalidateBackground();
	}

	int total = search_results ? search_results->count : 0;

	bool had_thumb = false;
	int ox = screen->w;

	if (total > 0 && CFG_getShowGameArt()) {
		Entry* selected_entry = search_results->items[search_view.selected];

		char thumbpath[1024];
		ROM_mediaArtPath(selected_entry->path, thumbpath, sizeof(thumbpath));
		had_thumb = startLoadThumb(thumbpath);
		int max_w = (int)(screen->w - (screen->w * CFG_getGameArtWidth()));
		if (had_thumb)
			ox = (int)(max_w)-SCALE1(BUTTON_MARGIN * 5);
	}

	search_view.count = total;
	search_view.get_row = search_get_row;
	search_view.font = font.large;
	search_view.list_id = (const void*)search_results;
	// The thumbnail width adjustment is uniform across all rows.
	search_view.max_width_override =
		had_thumb ? MAX(0, ox + SCALE1(BUTTON_MARGIN) - SCALE1(PADDING * 2)) : 0;
	search_view.empty_title = NULL; // keep today's centered message instead
	if (total == 0) {
		UI_renderCenteredMessage(screen, "No results");
	}
	// Hint bar in game-list order: B BACK, Y NETPLAY, X RESUME, A OPEN.
	// Static: the ListView keeps the pointer past this call.
	static char* hints[10];
	int p = 0;
	hints[p++] = "B";
	hints[p++] = "BACK";
	if (total > 0) {
		Entry* hint_entry = search_results->items[search_view.selected];
		readyResume(hint_entry); // refresh the shared resume state per selection
		if (GameList_entryNetplayCapable(hint_entry)) {
			hints[p++] = "Y";
			hints[p++] = "NETPLAY";
		}
		if (resume.can_resume) {
			hints[p++] = "X";
			hints[p++] = "RESUME";
		}
		hints[p++] = "A";
		hints[p++] = "OPEN";
	}
	hints[p] = NULL;
	search_view.hint_pairs = hints;
	UI_listViewRender(&search_view, screen);
}
