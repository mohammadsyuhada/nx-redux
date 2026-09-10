// osdmusic — resident renderer for the trimui_osdd "Music" app widget.
//
// trimui_osdd runs widgets/app_music/launch.sh once at startup, then opens
// the widget's command FIFO for writing and its canvas file read-only
// (config.json: "cmd", "canvasv", 412x268 ARGB8888 = the 3x2 block tile).
// It writes one line per event into the FIFO ("enable" when the widget takes
// focus, "left"/"right"/"key_a" while focused, "disable"/"key_b" when it
// loses it) and re-reads the canvas at "canvasfps".
//
// This process owns both endpoints and draws the widget: cover art, title,
// artist and a prev / play-pause / next transport row. Everything it shows is
// DUMMY state kept in this file — there is no music owner behind it yet. A
// real player (PR #92) is expected to keep this exact widget contract and
// replace the `state` block with live data plus real transport actions.

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
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
#define MUSIC_PROBE_MS 1000
// What "music running" means for the placeholder until a real owner exists:
// the Music Player tool is open. Pressing A on the placeholder asks nextui to
// launch that pak through OPEN_PAK_REQUEST_PATH (see common/defines.h).
#define MUSIC_PROCESS "musicplayer.elf"
#define MUSIC_PAK "/mnt/SDCARD/.system/paks/Tools/Music Player.pak"
#define LAUNCHER_PROCESS "nextui.elf"
#define OPEN_PAK_REQUEST_PATH "/tmp/nextui_open"
#define OSD_HIDE_PATH "/tmp/hide_osdd"
#define INPUT_LIMIT 512

enum { FOCUS_PREV = 0,
	   FOCUS_PLAY = 1,
	   FOCUS_NEXT = 2,
	   FOCUS_COUNT = 3 };

// ---- dummy content -------------------------------------------------------
// Replace with a snapshot from the real player. Nothing below this block
// knows where the values come from.
static const struct {
	const char* title;
	const char* artist;
} tracks[] = {
	{"Song Title", "Artist Name"},
	{"Another Song", "Another Artist"},
	{"Third Track", "Some Band"},
};
static struct {
	int track;
	bool playing;
	int focus;		// which transport button the d-pad is on
	bool enabled;	// true while trimui_osdd has this widget focused
	bool has_music; // a music owner is running (see MUSIC_PROCESS)
} state = {.track = 0, .playing = false, .focus = FOCUS_PLAY, .enabled = false, .has_music = false};
// --------------------------------------------------------------------------

static volatile sig_atomic_t quit;
static void on_signal(int sig) {
	(void)sig;
	quit = 1;
}

typedef struct {
	SDL_Surface* frame; // ARGB8888, same layout as the canvas
	Uint8* canvas;		// mmap of CANVAS_PATH
	SDL_Surface* cover;
	SDL_Surface* backdrop;
	SDL_Surface* prev_icon;
	SDL_Surface* play_icon;
	SDL_Surface* pause_icon;
	SDL_Surface* next_icon;
	TTF_Font* title_font;
	TTF_Font* artist_font;
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
		fill_circle(w->frame, cx, cy, disc, 255, 255, 255);
		blit_tinted(w->frame, icon, cx - icon->w / 2, cy - icon->h / 2, 48, 48, 48);
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

static void render_placeholder(Widget* w) {
	SDL_Surface* f = w->frame;
	const int title_h = w->title_font ? TTF_FontHeight(w->title_font) : 30;
	const int hint_h = w->artist_font ? TTF_FontHeight(w->artist_font) : 22;
	int y = (CANVAS_H - (title_h + 6 + hint_h)) / 2;
	SDL_Color white = {255, 255, 255, 255};
	SDL_Color grey = {170, 170, 170, 255};
	draw_text_centered(f, w->title_font, "No music playing", CANVAS_W / 2, y, white, CANVAS_W - 48);
	y += title_h + 6;
	// The hint brightens once the widget is entered, when A will act on it.
	draw_text_centered(f, w->artist_font, "Press A to open Music Player", CANVAS_W / 2, y,
					   state.enabled ? white : grey, CANVAS_W - 48);
}

static void render(Widget* w) {
	SDL_Surface* f = w->frame;
	// Transparent: trimui_osdd composites the canvas over its own block tile.
	SDL_FillRect(f, NULL, SDL_MapRGBA(f->format, 0, 0, 0, 0));
	if (!state.has_music) {
		render_placeholder(w);
		if (memcmp(w->canvas, f->pixels, CANVAS_BYTES) != 0)
			memcpy(w->canvas, f->pixels, CANVAS_BYTES);
		return;
	}
	blit_at(f, w->backdrop, 0, 0);

	// Title, artist and the transport row form one block, centred both ways.
	const int title_h = w->title_font ? TTF_FontHeight(w->title_font) : 30;
	const int artist_h = w->artist_font ? TTF_FontHeight(w->artist_font) : 22;
	const int disc = 42;
	const int block_h = title_h + 4 + artist_h + 8 + disc * 2;
	int y = (CANVAS_H - block_h) / 2;
	const int cx = CANVAS_W / 2;

	SDL_Color white = {255, 255, 255, 255};
	SDL_Color grey = {200, 200, 200, 255};
	draw_text_centered(f, w->title_font, tracks[state.track].title, cx, y, white, CANVAS_W - 48);
	y += title_h + 4;
	draw_text_centered(f, w->artist_font, tracks[state.track].artist, cx, y, grey, CANVAS_W - 48);
	y += artist_h + 8 + disc;

	bool focus_visible = state.enabled;
	draw_button(w, w->prev_icon, cx - 84, y, 36, focus_visible && state.focus == FOCUS_PREV);
	draw_button(w, state.playing ? w->pause_icon : w->play_icon, cx, y, disc,
				focus_visible && state.focus == FOCUS_PLAY);
	draw_button(w, w->next_icon, cx + 84, y, 36, focus_visible && state.focus == FOCUS_NEXT);

	// Publish only when something changed; the daemon re-reads at canvasfps.
	if (memcmp(w->canvas, f->pixels, CANVAS_BYTES) != 0)
		memcpy(w->canvas, f->pixels, CANVAS_BYTES);
}

// ---- dummy actions: swap these for real player commands ------------------
static void action_previous(void) {
	state.track = (state.track + (int)SDL_arraysize(tracks) - 1) % (int)SDL_arraysize(tracks);
}
static void action_toggle(void) {
	state.playing = !state.playing;
}
static void action_next(void) {
	state.track = (state.track + 1) % (int)SDL_arraysize(tracks);
}
// --------------------------------------------------------------------------

static void handle_command(const char* cmd) {
	if (!strcmp(cmd, "enable")) {
		state.enabled = true;
		state.focus = FOCUS_PLAY; // always land on play/pause, never on a stale button
	} else if (!strcmp(cmd, "disable") || !strcmp(cmd, "key_b")) {
		state.enabled = false;
	} else if (!state.enabled) {
		return;
	} else if (!strcmp(cmd, "left")) {
		state.focus = (state.focus + FOCUS_COUNT - 1) % FOCUS_COUNT;
	} else if (!strcmp(cmd, "right")) {
		state.focus = (state.focus + 1) % FOCUS_COUNT;
	} else if (!strcmp(cmd, "key_a")) {
		if (!state.has_music) {
			request_music_player();
			return;
		}
		if (state.focus == FOCUS_PREV)
			action_previous();
		else if (state.focus == FOCUS_PLAY)
			action_toggle();
		else
			action_next();
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
				handle_command(w->input);
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

int main(void) {
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
	if (SDL_Init(0) != 0 || TTF_Init() != 0 || !(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
		goto cleanup;
	w.title_font = TTF_OpenFont(FONT_PATH, 26);
	if (!w.title_font)
		w.title_font = TTF_OpenFont(FONT_FALLBACK, 26);
	w.artist_font = TTF_OpenFont(FONT_PATH, 19);
	if (!w.artist_font)
		w.artist_font = TTF_OpenFont(FONT_FALLBACK, 19);
	w.cover = load_png("widget-cover-default.png", 90);
	w.backdrop = make_backdrop(w.cover);
	w.prev_icon = load_png("btn-prev-n.png", 44);
	w.play_icon = load_png("btn-play-n.png", 52);
	w.pause_icon = load_png("btn-pause-n.png", 52);
	w.next_icon = load_png("ic-next-n.png", 44);
	if (open_canvas(&w) != 0)
		goto cleanup;
	w.cmd_fd = open_fifo();
	if (w.cmd_fd < 0)
		goto cleanup;

	state.has_music = process_running(MUSIC_PROCESS);
	render(&w);
	write_ready();
	rc = 0;
	Uint32 next_probe = SDL_GetTicks() + MUSIC_PROBE_MS;
	while (!quit) {
		if ((Sint32)(SDL_GetTicks() - next_probe) >= 0) {
			next_probe = SDL_GetTicks() + MUSIC_PROBE_MS;
			bool running = process_running(MUSIC_PROCESS);
			if (running != state.has_music) {
				state.has_music = running;
				render(&w);
			}
		}
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
	IMG_Quit();
	if (TTF_WasInit())
		TTF_Quit();
	SDL_Quit();
	close(lock_fd);
	return rc;
}
