// osdmusic — resident renderer for the trimui_osdd "Music" app widget.
//
// trimui_osdd runs widgets/app_music/launch.sh once at startup, then opens
// the widget's command FIFO for writing and its canvas file read-only
// (config.json: "cmd", "canvasv", 412x268 ARGB8888 = the 3x2 block tile).
// It writes one line per event into the FIFO ("enable" when the widget takes
// focus, "left"/"right"/"key_a" while focused, "disable"/"key_b" when it
// loses it) and re-reads the canvas at "canvasfps".
//
// This process owns the endpoints and renders daemon snapshots without owning
// playback. The background daemon remains the sole queue and audio owner.

#include "../musicplayer/music_balance.h"
#include "../musicplayer/music_client.h"
#if defined(PLATFORM_TG5050)
#include "../../tg5050/libmsettings/msettings.h"
#else
#include "../../tg5040/libmsettings/msettings.h"
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <zlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Canvas geometry comes from the widget's config.json at startup (the tile
// is 3x2 = 412x268 on the 1024x768 models and 4x2 = 540x260 on 1280x720).
static int CANVAS_W = 412;
static int CANVAS_H = 268;
static int GRID_W = 3;
static int GRID_H = 2;
#define CANVAS_BYTES ((size_t)CANVAS_W * CANVAS_H * 4)
#define CONFIG_PATH "/usr/trimui/osd/widgets/app_music/config.json"

#define MUSIC_DIR "/tmp/trimui_music"
#define CMD_PATH MUSIC_DIR "/widget_cmd"
#define CANVAS_PATH MUSIC_DIR "/vfb_osd"
#define READY_PATH MUSIC_DIR "/widget_ready"
#define LOCK_PATH MUSIC_DIR "/widget.lock"

#define SKIN_DIR "/usr/trimui/osd/widgets/app_music/skin"
#define FONT_PATH "/mnt/SDCARD/.system/res/font1.ttf"
#define FONT_FALLBACK "/usr/trimui/osd/regular.ttf"
#define TILE_DIR "/usr/trimui/osd"
#define BACKDROP_ALPHA 80 // 0-255: cover art opacity behind the text

#define POLL_MS 100
#define MUSIC_PROBE_MS 250
#define MUSIC_PAK "/mnt/SDCARD/.system/paks/Tools/Music Player.pak"
#define LAUNCHER_PROCESS "nextui.elf"
#define OPEN_PAK_REQUEST_PATH "/tmp/nextui_open"
#define OSD_HIDE_PATH "/tmp/hide_osdd"
#define OSD_SHOW_FLAG "/tmp/trimui_osd/osdd_show_up"
#define DEBUG_FLAG_PATH "/tmp/osdmusic_debug"
#define DEBUG_LOG_PATH "/tmp/osdmusic_cmd.log"
#define THEME_SETTINGS_PATH "/mnt/SDCARD/.userdata/shared/minuisettings.txt"
#define INPUT_LIMIT 512
#define BALANCE_MAX 10

enum { FOCUS_PREV = 0,
	   FOCUS_PLAY = 1,
	   FOCUS_NEXT = 2,
	   FOCUS_COUNT = 3 };
enum { ROW_TRANSPORT = 0,
	   ROW_BALANCE = 1 };

static struct {
	MusicSnapshotWire snapshot;
	bool connected;
	bool enabled;
	int focus;
	int row;
	char artwork_identity[MUSIC_SERVICE_MAX_PATH * 2 + 2];
} state = {.focus = FOCUS_PLAY, .row = ROW_TRANSPORT};


static volatile sig_atomic_t quit;

static SDL_Color accent = {54, 255, 160, 255}; // trimui_osdd focus green
static time_t accent_mtime;

static void load_accent(void) {
	struct stat st;
	if (stat(THEME_SETTINGS_PATH, &st) != 0 || st.st_mtime == accent_mtime)
		return;
	accent_mtime = st.st_mtime;
	SDL_Color next = {54, 255, 160, 255};
	FILE* f = fopen(THEME_SETTINGS_PATH, "r");
	if (f) {
		char line[128];
		while (fgets(line, sizeof(line), f)) {
			unsigned value;
			if (sscanf(line, "color2=0x%x", &value) == 1 || sscanf(line, "color2=0X%x", &value) == 1) {
				if (value == 0x002222ffU)
					value = 0x006666ffU;
				Uint8 r = (value >> 24) & 0xff, g = (value >> 16) & 0xff, b = (value >> 8) & 0xff;
				next = (SDL_Color){r, g, b, 255};
				break;
			}
		}
		fclose(f);
	}
	accent = next;
}
static void on_signal(int sig) {
	(void)sig;
	quit = 1;
}

typedef struct {
	SDL_Surface* frame;
	Uint8* canvas;
	SDL_Surface* cover;
	SDL_Surface* backdrop;
	SDL_Surface* prev_icon;
	SDL_Surface* play_icon;
	SDL_Surface* pause_icon;
	SDL_Surface* next_icon;
	TTF_Font* title_font;
	TTF_Font* artist_font;
	TTF_Font* label_font;
	int cmd_fd;
	char input[INPUT_LIMIT];
	size_t input_len;
} Widget;

// Bilinear resample with premultiplied-alpha weighting (SDL_BlitScaled is
// nearest-neighbour and leaves scaled glyphs jagged).
static SDL_Surface* resample(SDL_Surface* src, int w, int h) {
	SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!dst)
		return NULL;
	SDL_LockSurface(src);
	for (int y = 0; y < h; y++) {
		double sy = ((y + 0.5) * src->h) / h - 0.5;
		int y0 = (int)SDL_floor(sy), y1 = y0 + 1;
		double fy = sy - y0;
		if (y0 < 0)
			y0 = 0;
		if (y1 > src->h - 1)
			y1 = src->h - 1;
		Uint32* out = (Uint32*)((Uint8*)dst->pixels + y * dst->pitch);
		for (int x = 0; x < w; x++) {
			double sx = ((x + 0.5) * src->w) / w - 0.5;
			int x0 = (int)SDL_floor(sx), x1 = x0 + 1;
			double fx = sx - x0;
			if (x0 < 0)
				x0 = 0;
			if (x1 > src->w - 1)
				x1 = src->w - 1;
			double acc[4] = {0, 0, 0, 0};
			const int xs[2] = {x0, x1}, ys[2] = {y0, y1};
			const double wx[2] = {1 - fx, fx}, wy[2] = {1 - fy, fy};
			for (int j = 0; j < 2; j++)
				for (int i = 0; i < 2; i++) {
					Uint32 p = ((Uint32*)((Uint8*)src->pixels + ys[j] * src->pitch))[xs[i]];
					Uint8 r, g, b, a;
					SDL_GetRGBA(p, src->format, &r, &g, &b, &a);
					double wgt = wx[i] * wy[j], pa = a / 255.0;
					acc[0] += r * pa * wgt;
					acc[1] += g * pa * wgt;
					acc[2] += b * pa * wgt;
					acc[3] += pa * wgt;
				}
			Uint8 r = 0, g = 0, b = 0, a = (Uint8)(acc[3] * 255 + 0.5);
			if (acc[3] > 0) {
				r = (Uint8)SDL_min(255, acc[0] / acc[3] + 0.5);
				g = (Uint8)SDL_min(255, acc[1] / acc[3] + 0.5);
				b = (Uint8)SDL_min(255, acc[2] / acc[3] + 0.5);
			}
			out[x] = SDL_MapRGBA(dst->format, r, g, b, a);
		}
	}
	SDL_UnlockSurface(src);
	SDL_SetSurfaceBlendMode(dst, SDL_BLENDMODE_BLEND);
	return dst;
}

static SDL_Surface* load_png_path(const char* path, int w, int h) {
	SDL_Surface* loaded = IMG_Load(path);
	if (!loaded)
		return NULL;
	SDL_Surface* rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(loaded);
	if (!rgba || (rgba->w == w && rgba->h == h)) {
		if (rgba)
			SDL_SetSurfaceBlendMode(rgba, SDL_BLENDMODE_BLEND);
		return rgba;
	}
	SDL_Surface* scaled = resample(rgba, w, h);
	SDL_FreeSurface(rgba);
	return scaled;
}

static SDL_Surface* load_png(const char* name, int size) {
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", SKIN_DIR, name);
	return load_png_path(path, size, size);
}

// Pull "key" : number out of the widget's config.json (flat JSON, no nesting).
static int config_int(const char* json, const char* key, int fallback) {
	char needle[64];
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	const char* at = strstr(json, needle);
	if (!at)
		return fallback;
	at = strchr(at + strlen(needle), ':');
	if (!at)
		return fallback;
	int value = fallback;
	if (sscanf(at + 1, " %d", &value) != 1)
		return fallback;
	return value;
}

static void load_geometry(void) {
	FILE* f = fopen(CONFIG_PATH, "r");
	if (!f)
		return;
	char json[2048];
	size_t n = fread(json, 1, sizeof(json) - 1, f);
	fclose(f);
	json[n] = '\0';
	CANVAS_W = config_int(json, "canvaswidth", CANVAS_W);
	CANVAS_H = config_int(json, "canvasheight", CANVAS_H);
	GRID_W = config_int(json, "gridwidth", GRID_W);
	GRID_H = config_int(json, "gridheight", GRID_H);
}

// Widget background: the cover art scaled to fill the tile, dimmed, and
// masked by the block tile's own alpha so it keeps the rounded corners.
static SDL_Surface* make_backdrop(SDL_Surface* cover_src) {
	if (!cover_src)
		return NULL;
	int side = CANVAS_W > CANVAS_H ? CANVAS_W : CANVAS_H;
	SDL_Surface* big = resample(cover_src, side, side);
	if (!big)
		return NULL;
	char tile[128];
	snprintf(tile, sizeof(tile), "%s/block%dx%d.png", TILE_DIR, GRID_W, GRID_H);
	SDL_Surface* mask = load_png_path(tile, CANVAS_W, CANVAS_H);
	SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, CANVAS_W, CANVAS_H, 32, SDL_PIXELFORMAT_ARGB8888);
	if (out) {
		int ox = (side - CANVAS_W) / 2, oy = (side - CANVAS_H) / 2;
		for (int y = 0; y < CANVAS_H; y++) {
			Uint32* row = (Uint32*)((Uint8*)out->pixels + y * out->pitch);
			for (int x = 0; x < CANVAS_W; x++) {
				Uint8 r, g, b, a, ma = 255;
				SDL_GetRGBA(((Uint32*)((Uint8*)big->pixels + (y + oy) * big->pitch))[x + ox], big->format, &r, &g,
							&b, &a);
				if (mask) {
					Uint8 mr, mg, mb;
					SDL_GetRGBA(((Uint32*)((Uint8*)mask->pixels + y * mask->pitch))[x], mask->format, &mr, &mg, &mb,
								&ma);
				}
				row[x] = SDL_MapRGBA(out->format, r, g, b, (Uint8)(a * ma / 255 * BACKDROP_ALPHA / 255));
			}
		}
		SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_BLEND);
	}
	if (mask)
		SDL_FreeSurface(mask);
	SDL_FreeSurface(big);
	return out;
}

static void blit_at(SDL_Surface* dst, SDL_Surface* src, int x, int y) {
	if (!src)
		return;
	SDL_Rect at = {.x = x, .y = y, .w = src->w, .h = src->h};
	SDL_BlitSurface(src, NULL, dst, &at);
}

// Recolour an icon's RGB (keeping alpha) so the same glyph can be drawn white
// on the tile or dark on the white focus disc.
static void blit_tinted(SDL_Surface* dst, SDL_Surface* icon, int x, int y, Uint8 r, Uint8 g, Uint8 b) {
	if (!icon)
		return;
	SDL_SetSurfaceColorMod(icon, r, g, b);
	blit_at(dst, icon, x, y);
	SDL_SetSurfaceColorMod(icon, 255, 255, 255);
}

// Anti-aliased filled disc: per-pixel coverage from the distance to the
// edge, blended "over" whatever is already in the frame.
static void fill_circle(SDL_Surface* dst, int cx, int cy, int radius, Uint8 r, Uint8 g, Uint8 b) {
	for (int y = cy - radius - 1; y <= cy + radius + 1; y++) {
		if (y < 0 || y >= dst->h)
			continue;
		Uint32* row = (Uint32*)((Uint8*)dst->pixels + y * dst->pitch);
		for (int x = cx - radius - 1; x <= cx + radius + 1; x++) {
			if (x < 0 || x >= dst->w)
				continue;
			double d = SDL_sqrt((double)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
			double cov = radius + 0.5 - d;
			if (cov <= 0)
				continue;
			if (cov > 1)
				cov = 1;
			Uint8 dr, dg, db, da;
			SDL_GetRGBA(row[x], dst->format, &dr, &dg, &db, &da);
			double bg = (1 - cov) * (da / 255.0);
			double oa = cov + bg;
			Uint8 orr = (Uint8)((r * cov + dr * bg) / oa + 0.5);
			Uint8 og = (Uint8)((g * cov + dg * bg) / oa + 0.5);
			Uint8 ob = (Uint8)((b * cov + db * bg) / oa + 0.5);
			row[x] = SDL_MapRGBA(dst->format, orr, og, ob, (Uint8)(oa * 255 + 0.5));
		}
	}
}

static int text_width(TTF_Font* font, const char* text) {
	int w = 0;
	if (font && text)
		TTF_SizeUTF8(font, text, &w, NULL);
	return w;
}

static void draw_text_centered(SDL_Surface* dst, TTF_Font* font, const char* text, int cx, int y, SDL_Color color,
							   int max_w) {
	if (!font || !text || !*text)
		return;
	char clipped[128];
	snprintf(clipped, sizeof(clipped), "%s", text);
	while (text_width(font, clipped) > max_w && strlen(clipped) > 4)
		strcpy(clipped + strlen(clipped) - 4, "...");
	SDL_Surface* s = TTF_RenderUTF8_Blended(font, clipped, color);
	if (!s)
		return;
	blit_at(dst, s, cx - s->w / 2, y);
	SDL_FreeSurface(s);
}

// A transport button: white glyph on the tile, or a white disc with a dark
// glyph while the d-pad is on it (the same look the OSD's own toggles use).
static void draw_button(Widget* w, SDL_Surface* icon, int cx, int cy, int disc, bool focused) {
	if (!icon)
		return;
	if (focused) {
		fill_circle(w->frame, cx, cy, disc, accent.r, accent.g, accent.b);
		const int luminance = 299 * accent.r + 587 * accent.g + 114 * accent.b;
		const Uint8 icon_color = luminance < 128000 ? 255 : 32;
		blit_tinted(w->frame, icon, cx - icon->w / 2, cy - icon->h / 2,
					icon_color, icon_color, icon_color);
	} else {
		blit_tinted(w->frame, icon, cx - icon->w / 2, cy - icon->h / 2, 255, 255, 255);
	}
}

// pidof without spawning a shell: scan /proc/*/comm.
static bool process_running(const char* comm_name) {
	DIR* proc = opendir("/proc");
	if (!proc)
		return false;
	bool found = false;
	struct dirent* entry;
	while (!found && (entry = readdir(proc))) {
		if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
			continue;
		char path[64];
		snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
		FILE* f = fopen(path, "r");
		if (!f)
			continue;
		char comm[64] = {0};
		if (fgets(comm, sizeof(comm), f)) {
			comm[strcspn(comm, "\n")] = '\0';
			found = strcmp(comm, comm_name) == 0;
		}
		fclose(f);
	}
	closedir(proc);
	return found;
}

static void request_music_player(void) {
	// Only the launcher consumes the request; while a game or another tool is
	// in the foreground there is nothing to hand the pak to.
	if (!process_running(LAUNCHER_PROCESS))
		return;
	FILE* f = fopen(OPEN_PAK_REQUEST_PATH, "w");
	if (!f)
		return;
	fprintf(f, "%s\n", MUSIC_PAK);
	fclose(f);
	// Close the panel so the pad goes back to the app that is about to start.
	f = fopen(OSD_HIDE_PATH, "w");
	if (f)
		fclose(f);
}

static bool music_active(void) {
	return state.connected && state.snapshot.loaded;
}

static const char* snapshot_title(void) {
	if (state.snapshot.title[0])
		return state.snapshot.title;
	if (state.snapshot.source == MUSIC_SOURCE_RADIO && state.snapshot.album[0])
		return state.snapshot.album;
	const char* name = strrchr(state.snapshot.current_file, '/');
	return name ? name + 1 : state.snapshot.current_file;
}

static const char* snapshot_artist(void) {
	if (state.snapshot.artist[0])
		return state.snapshot.artist;
	if (state.snapshot.source == MUSIC_SOURCE_RADIO && state.snapshot.title[0] && state.snapshot.album[0])
		return state.snapshot.album;
	return state.snapshot.source == MUSIC_SOURCE_RADIO ? "Radio" : state.snapshot.source == MUSIC_SOURCE_PODCAST ? "Podcast"
																												 : "";
}

static void clear_artwork(Widget* w) {
	if (w->cover)
		SDL_FreeSurface(w->cover);
	if (w->backdrop)
		SDL_FreeSurface(w->backdrop);
	w->cover = NULL;
	w->backdrop = NULL;
}

static void sync_music(Widget* w, bool poll_owner) {
	if (poll_owner)
		MusicClient_update();
	state.snapshot = *MusicClient_snapshot();
	state.connected = MusicClient_isConnected();
	char identity[sizeof(state.artwork_identity)];
	if (music_active())
		snprintf(identity, sizeof(identity), "%s:%s", state.snapshot.current_file, state.snapshot.artwork_path);
	else
		identity[0] = '\0';
	if (strcmp(identity, state.artwork_identity) == 0)
		return;
	clear_artwork(w);
	snprintf(state.artwork_identity, sizeof(state.artwork_identity), "%s", identity);
	if (music_active()) {
		w->cover = state.snapshot.artwork_path[0] ? load_png_path(state.snapshot.artwork_path, 90, 90) : NULL;
		if (!w->cover)
			w->cover = load_png("widget-cover-default.png", 90);
		w->backdrop = make_backdrop(w->cover);
	}
}

static void draw_balance_row(Widget* w, int y);

static void render_placeholder(Widget* w) {
	SDL_Surface* f = w->frame;
	const int title_h = w->title_font ? TTF_FontHeight(w->title_font) : 30;
	const int hint_h = w->artist_font ? TTF_FontHeight(w->artist_font) : 22;
	const int balance_gap = 18, balance_h = 30;
	int y = (CANVAS_H - (title_h + 6 + hint_h + balance_gap + balance_h)) / 2;
	SDL_Color white = {255, 255, 255, 255};
	SDL_Color grey = {170, 170, 170, 255};
	draw_text_centered(f, w->title_font, "No music playing", CANVAS_W / 2, y, white, CANVAS_W - 48);
	y += title_h + 6;
	draw_text_centered(f, w->artist_font, "Press A to open Music Player", CANVAS_W / 2, y,
					   state.enabled ? accent : grey, CANVAS_W - 48);
	draw_balance_row(w, y + hint_h + balance_gap + balance_h / 2);
}

static void draw_balance_row(Widget* w, int y) {
	SDL_Surface* f = w->frame;
	const bool focused = state.enabled && state.row == ROW_BALANCE;
	const int bar_x = 48, bar_w = CANVAS_W - 96, bar_h = 6;
	const int balance = MusicBalance_getValue();
	SDL_Color white = {255, 255, 255, 255};
	SDL_Color grey = {170, 170, 170, 255};
	Uint32 fg = focused ? SDL_MapRGBA(f->format, accent.r, accent.g, accent.b, 255)
						: SDL_MapRGBA(f->format, 170, 170, 170, 255);
	Uint32 track = SDL_MapRGBA(f->format, 90, 90, 90, 255);
	SDL_FillRect(f, &(SDL_Rect){bar_x, y - bar_h / 2, bar_w, bar_h}, track);
	int centre_x = bar_x + bar_w / 2;
	int knob_x = bar_x + bar_w * balance / BALANCE_MAX;
	int from = knob_x < centre_x ? knob_x : centre_x;
	int to = knob_x < centre_x ? centre_x : knob_x;
	if (to > from)
		SDL_FillRect(f, &(SDL_Rect){from, y - bar_h / 2, to - from, bar_h}, fg);
	SDL_FillRect(f, &(SDL_Rect){centre_x - 1, y - 9, 2, 18}, fg);
	fill_circle(f, knob_x, y, focused ? 8 : 5, focused ? accent.r : 255, focused ? accent.g : 255,
				focused ? accent.b : 255);
	int ly = y + 10;
	int gw = text_width(w->label_font, "Game");
	int mw = text_width(w->label_font, "Music");
	draw_text_centered(f, w->label_font, "Game", bar_x + gw / 2, ly, focused ? white : grey, 80);
	draw_text_centered(f, w->label_font, MusicBalance_getDisplayString(), CANVAS_W / 2, ly,
					   focused ? accent : grey, 120);
	draw_text_centered(f, w->label_font, "Music", bar_x + bar_w - mw / 2, ly, focused ? white : grey, 80);
}

static void render(Widget* w) {
	SDL_Surface* f = w->frame;
	SDL_FillRect(f, NULL, SDL_MapRGBA(f->format, 0, 0, 0, 0));
	if (!music_active()) {
		render_placeholder(w);
	} else {
		blit_at(f, w->backdrop, 0, 0);
		const int title_h = w->title_font ? TTF_FontHeight(w->title_font) : 30;
		const int artist_h = w->artist_font ? TTF_FontHeight(w->artist_font) : 22;
		const int disc = 42, balance_gap = 18, balance_h = 30;
		int y = (CANVAS_H - (title_h + 4 + artist_h + 8 + disc * 2 + balance_gap + balance_h)) / 2;
		const int cx = CANVAS_W / 2;
		SDL_Color white = {255, 255, 255, 255};
		SDL_Color grey = {200, 200, 200, 255};
		draw_text_centered(f, w->title_font, snapshot_title(), cx, y, white, CANVAS_W - 48);
		y += title_h + 4;
		draw_text_centered(f, w->artist_font, snapshot_artist(), cx, y, grey, CANVAS_W - 48);
		y += artist_h + 8 + disc - 10;
		bool focus_visible = state.enabled && state.row == ROW_TRANSPORT;
		draw_button(w, w->prev_icon, cx - 84, y, 36, focus_visible && state.focus == FOCUS_PREV);
		draw_button(w, state.snapshot.state == MUSIC_STATE_PLAYING ? w->pause_icon : w->play_icon, cx, y, disc,
					focus_visible && state.focus == FOCUS_PLAY);
		draw_button(w, w->next_icon, cx + 84, y, 36, focus_visible && state.focus == FOCUS_NEXT);
		draw_balance_row(w, y + disc + balance_gap + 4);
	}
	if (memcmp(w->canvas, f->pixels, CANVAS_BYTES) != 0)
		memcpy(w->canvas, f->pixels, CANVAS_BYTES);
}

static bool can_control(uint32_t capability) {
	return music_active() && (state.snapshot.capabilities & capability);
}

static void action(Widget* w, int focus) {
	if (focus == FOCUS_PREV && can_control(MUSIC_CAP_PREVIOUS))
		(void)MusicClient_previous();
	else if (focus == FOCUS_NEXT && can_control(MUSIC_CAP_NEXT))
		(void)MusicClient_next();
	else if (focus == FOCUS_PLAY && can_control(state.snapshot.state == MUSIC_STATE_PLAYING ? MUSIC_CAP_PAUSE : MUSIC_CAP_PLAY))
		(void)MusicClient_toggle();
	else
		return;
	sync_music(w, false);
}

static void trace_command(const char* cmd) {
	if (access(DEBUG_FLAG_PATH, F_OK) != 0)
		return;
	FILE* f = fopen(DEBUG_LOG_PATH, "a");
	if (f) {
		fprintf(f, "%s\n", cmd);
		fclose(f);
	}
}

static void handle_command(Widget* w, const char* cmd) {
	trace_command(cmd);
	if (!strcmp(cmd, "enable")) {
		state.enabled = true;
		state.focus = FOCUS_PLAY;
		state.row = ROW_TRANSPORT;
	} else if (!strcmp(cmd, "disable") || !strcmp(cmd, "key_b")) {
		state.enabled = false;
	} else if (!state.enabled) {
		return;
	} else if (!strcmp(cmd, "down")) {
		state.row = ROW_BALANCE;
	} else if (!strcmp(cmd, "up")) {
		state.row = ROW_TRANSPORT;
	} else if (state.row == ROW_BALANCE) {
		if (!strcmp(cmd, "left") || !strcmp(cmd, "right")) {
			int value = MusicBalance_getValue() + (cmd[0] == 'l' ? -1 : 1);
			(void)MusicBalance_setValue(value);
			return;
		}
	} else if (!strcmp(cmd, "left")) {
		state.focus = (state.focus + FOCUS_COUNT - 1) % FOCUS_COUNT;
	} else if (!strcmp(cmd, "right")) {
		state.focus = (state.focus + 1) % FOCUS_COUNT;
	} else if (!strcmp(cmd, "key_a")) {
		if (!music_active())
			request_music_player();
		else
			action(w, state.focus);
	}
}

static int open_fifo(void) {
	struct stat st;
	if (lstat(CMD_PATH, &st) == 0 && !S_ISFIFO(st.st_mode))
		unlink(CMD_PATH);
	if (mkfifo(CMD_PATH, 0600) != 0 && errno != EEXIST)
		return -1;
	return open(CMD_PATH, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
}

static void read_commands(Widget* w) {
	for (;;) {
		if (w->input_len >= sizeof(w->input) - 1)
			w->input_len = 0;
		ssize_t n = read(w->cmd_fd, w->input + w->input_len, sizeof(w->input) - 1 - w->input_len);
		if (n > 0) {
			w->input_len += (size_t)n;
			w->input[w->input_len] = '\0';
			char* nl;
			while ((nl = memchr(w->input, '\n', w->input_len))) {
				*nl = '\0';
				handle_command(w, w->input);
				size_t used = (size_t)(nl - w->input) + 1;
				memmove(w->input, w->input + used, w->input_len - used);
				w->input_len -= used;
			}
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
			return;
		// EOF: the writer closed. Reopen so the next open by trimui_osdd pairs up.
		close(w->cmd_fd);
		w->cmd_fd = open_fifo();
		w->input_len = 0;
		return;
	}
}

static int open_canvas(Widget* w) {
	int fd = open(CANVAS_PATH, O_RDWR | O_CREAT, 0600);
	if (fd < 0)
		return -1;
	if (ftruncate(fd, CANVAS_BYTES) != 0) {
		close(fd);
		return -1;
	}
	w->canvas = mmap(NULL, CANVAS_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (w->canvas == MAP_FAILED) {
		w->canvas = NULL;
		return -1;
	}
	w->frame = SDL_CreateRGBSurfaceWithFormat(0, CANVAS_W, CANVAS_H, 32, SDL_PIXELFORMAT_ARGB8888);
	return w->frame && w->frame->pitch == CANVAS_W * 4 ? 0 : -1;
}

static void write_ready(void) {
	FILE* f = fopen(READY_PATH, "w");
	if (f) {
		fprintf(f, "%ld\n", (long)getpid());
		fclose(f);
	}
}

// ---- OSD focus-colour tint ---------------------------------------------
// trimui_osdd paints its focus ring (block*_sel.png), the active slider
// (progress_*_sel.png) and the toast frame (bg_msg_w*.png) from images. The
// shipped ones are pure WHITE shapes (alpha carries the anti-aliasing). Run as
// `osdmusic.elf --tint-osd <src> <dst>` from the launcher before the daemon
// starts, this rewrites them with the theme accent: every white pixel becomes
// the accent scaled by the pixel's brightness, alpha untouched, so the whole
// OSD follows the theme (a white accent leaves them as shipped).
static const char* const TINT_FILES[] = {
	"block1x1_sel.png",
	"block1x2_sel.png",
	"block2x1_sel.png",
	"block2x2_sel.png",
	"block3x1_sel.png",
	"block3x2_sel.png",
	"block4x1_sel.png",
	"block4x2_sel.png",
	"progress_bg_sel.png",
	"progress_fg_sel.png",
	"bg_msg_w1.png",
	"bg_msg_w2.png",
	"bg_msg_w3.png",
	"bg_msg_w4.png",
};

static void png_chunk(FILE* f, const char* type, const unsigned char* data, size_t len) {
	unsigned char head[8] = {(unsigned char)(len >> 24), (unsigned char)(len >> 16), (unsigned char)(len >> 8),
							 (unsigned char)len, type[0], type[1], type[2], type[3]};
	fwrite(head, 1, 8, f);
	if (len)
		fwrite(data, 1, len, f);
	uLong crc = crc32(0L, head + 4, 4);
	if (len)
		crc = crc32(crc, data, len);
	unsigned char tail[4] = {(unsigned char)(crc >> 24), (unsigned char)(crc >> 16), (unsigned char)(crc >> 8),
							 (unsigned char)crc};
	fwrite(tail, 1, 4, f);
}

// Minimal RGBA8 PNG writer (this SDL_image predates IMG_SavePNG).
static int png_write(const char* path, SDL_Surface* rgba) {
	const int w = rgba->w, h = rgba->h;
	size_t raw_len = (size_t)h * (1 + (size_t)w * 4);
	unsigned char* raw = malloc(raw_len);
	if (!raw)
		return -1;
	for (int y = 0; y < h; y++) {
		unsigned char* row = raw + (size_t)y * (1 + (size_t)w * 4);
		row[0] = 0; // filter: none
		memcpy(row + 1, (const unsigned char*)rgba->pixels + y * rgba->pitch, (size_t)w * 4);
	}
	uLongf zlen = compressBound(raw_len);
	unsigned char* z = malloc(zlen);
	if (!z || compress2(z, &zlen, raw, raw_len, 6) != Z_OK) {
		free(raw);
		free(z);
		return -1;
	}
	FILE* f = fopen(path, "wb");
	if (!f) {
		free(raw);
		free(z);
		return -1;
	}
	static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
	fwrite(sig, 1, 8, f);
	unsigned char ihdr[13] = {(unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8), (unsigned char)w,
							  (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h,
							  8, 6, 0, 0, 0};
	png_chunk(f, "IHDR", ihdr, 13);
	png_chunk(f, "IDAT", z, zlen);
	png_chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(raw);
	free(z);
	return 0;
}

static int tint_osd(const char* src_dir, const char* dst_dir) {
	load_accent();
	if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) != IMG_INIT_PNG)
		return 1;
	int failures = 0;
	for (size_t i = 0; i < sizeof(TINT_FILES) / sizeof(TINT_FILES[0]); i++) {
		char in[512], out[512];
		snprintf(in, sizeof(in), "%s/%s", src_dir, TINT_FILES[i]);
		snprintf(out, sizeof(out), "%s/%s", dst_dir, TINT_FILES[i]);
		SDL_Surface* loaded = IMG_Load(in);
		if (!loaded)
			continue; // not every model ships every file
		SDL_Surface* img = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
		SDL_FreeSurface(loaded);
		if (!img) {
			failures++;
			continue;
		}
		for (int y = 0; y < img->h; y++) {
			unsigned char* px = (unsigned char*)img->pixels + y * img->pitch;
			for (int x = 0; x < img->w; x++, px += 4) {
				int r = px[0], g = px[1], b = px[2];
				int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
				int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
				if (px[3] && hi >= 96 && hi - lo <= 24) { // grey/white shape pixel
					px[0] = (unsigned char)(accent.r * hi / 255);
					px[1] = (unsigned char)(accent.g * hi / 255);
					px[2] = (unsigned char)(accent.b * hi / 255);
				}
			}
		}
		if (png_write(out, img) != 0)
			failures++;
		SDL_FreeSurface(img);
	}
	IMG_Quit();
	return failures ? 1 : 0;
}
// --------------------------------------------------------------------------

int main(int argc, char** argv) {
	if (argc == 4 && !strcmp(argv[1], "--tint-osd"))
		return tint_osd(argv[2], argv[3]);

	Widget w;
	memset(&w, 0, sizeof(w));
	w.cmd_fd = -1;
	load_geometry();
	mkdir(MUSIC_DIR, 0700);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	int lock_fd = open(LOCK_PATH, O_RDWR | O_CREAT, 0600);
	if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0)
		return 0; // another instance owns the widget
	unlink(READY_PATH);

	int rc = 1;
	bool settings_initialized = false;
	if (SDL_Init(0) != 0 || TTF_Init() != 0 || !(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
		goto cleanup;
	w.title_font = TTF_OpenFont(FONT_PATH, 26);
	if (!w.title_font)
		w.title_font = TTF_OpenFont(FONT_FALLBACK, 26);
	w.artist_font = TTF_OpenFont(FONT_PATH, 19);
	if (!w.artist_font)
		w.artist_font = TTF_OpenFont(FONT_FALLBACK, 19);
	w.label_font = TTF_OpenFont(FONT_PATH, 15);
	if (!w.label_font)
		w.label_font = TTF_OpenFont(FONT_FALLBACK, 15);
	w.prev_icon = load_png("btn-prev-n.png", 44);
	w.play_icon = load_png("btn-play-n.png", 52);
	w.pause_icon = load_png("btn-pause-n.png", 52);
	w.next_icon = load_png("ic-next-n.png", 44);
	if (open_canvas(&w) != 0)
		goto cleanup;
	w.cmd_fd = open_fifo();
	if (w.cmd_fd < 0)
		goto cleanup;

	InitSettings();
	settings_initialized = true;
	(void)MusicClient_init(NULL);
	load_accent();
	sync_music(&w, false);
	render(&w);
	write_ready();
	rc = 0;
	Uint32 next_probe = SDL_GetTicks() + MUSIC_PROBE_MS;
	bool panel_visible = false;
	while (!quit) {
		// Nothing composites the canvas while the OSD panel is hidden, so skip
		// all render/sync/accent work then and only wait on the command FIFO.
		bool visible = access(OSD_SHOW_FLAG, F_OK) == 0;
		// While hidden the widget must hold no socket, so an idle owner can exit.
		// This one check also drops the socket the startup attach (MusicClient_init
		// or the initial sync) may have opened, on the first hidden pass.
		if (!visible && MusicClient_isConnected())
			MusicClient_disconnect();
		if (visible && !panel_visible) {
			// Panel just opened: the socket was dropped while hidden, so sync from
			// the owner before the first render. The compositor keeps showing the
			// last published canvas until this render lands, so nothing is
			// mid-frame.
			load_accent();
			sync_music(&w, true);
			render(&w);
			next_probe = SDL_GetTicks() + MUSIC_PROBE_MS;
		} else if (visible && (Sint32)(SDL_GetTicks() - next_probe) >= 0) {
			next_probe = SDL_GetTicks() + MUSIC_PROBE_MS;
			load_accent();
			sync_music(&w, true);
			render(&w);
		}
		panel_visible = visible;
		if (w.cmd_fd < 0) {
			w.cmd_fd = open_fifo();
			usleep(POLL_MS * 1000);
			continue;
		}
		struct pollfd pfd = {.fd = w.cmd_fd, .events = POLLIN};
		if (poll(&pfd, 1, POLL_MS) > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
			read_commands(&w);
			render(&w);
		}
	}

cleanup:
	unlink(READY_PATH);
	if (w.cmd_fd >= 0)
		close(w.cmd_fd);
	if (w.canvas)
		munmap(w.canvas, CANVAS_BYTES);
	SDL_Surface* surfaces[] = {w.frame, w.cover, w.backdrop, w.prev_icon, w.play_icon, w.pause_icon, w.next_icon};
	for (size_t i = 0; i < SDL_arraysize(surfaces); i++)
		if (surfaces[i])
			SDL_FreeSurface(surfaces[i]);
	if (w.title_font)
		TTF_CloseFont(w.title_font);
	if (w.artist_font)
		TTF_CloseFont(w.artist_font);
	if (w.label_font)
		TTF_CloseFont(w.label_font);
	IMG_Quit();
	if (TTF_WasInit())
		TTF_Quit();
	SDL_Quit();
	MusicClient_quit();
	if (settings_initialized)
		QuitSettings();

	close(lock_fd);
	return rc;
}
