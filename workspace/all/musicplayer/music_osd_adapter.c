#include "music_client.h"
#if defined(PLATFORM_TG5050)
#include "../../tg5050/libmsettings/msettings.h"
#else
#include "../../tg5040/libmsettings/msettings.h"
#endif
#ifndef USE_SDL2
#define USE_SDL2 1
#endif
#include "../mediaplayer/include/ffplay/sdl2-headers/SDL.h"
typedef struct _TTF_Font TTF_Font;
int TTF_Init(void);
void TTF_Quit(void);
int TTF_WasInit(void);
TTF_Font* TTF_OpenFont(const char* file, int point_size);
void TTF_CloseFont(TTF_Font* font);
SDL_Surface* TTF_RenderUTF8_Blended(TTF_Font* font, const char* text, SDL_Color foreground);
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define OSD_WIDTH 260
#define OSD_HEIGHT 120
#define OSD_BYTES (OSD_WIDTH * OSD_HEIGHT * 4)
#define OSD_FONT_PATH "/usr/trimui/osd/regular.ttf"
#define OSD_INPUT_LIMIT 1024
#define OSD_POLL_MS 100
#define OSD_REFRESH_MS 500
#define OSD_SLIDER_MAX 10
#define OSD_VOLUME_STEP 2
#define OSD_BALANCE_SIDE_STEP 4

typedef enum {
	OSD_MODE_MUSIC,
	OSD_MODE_MASTER,
	OSD_MODE_BALANCE,
	OSD_MODE_BRIGHTNESS
} OSDMode;

typedef struct {
	const char* command;
	const char* canvas;
	const char* ready;
	const char* lock;
} OSDPaths;

static const OSDPaths OSD_PATHS[] = {
	[OSD_MODE_MUSIC] = {"/tmp/trimui_music/widget_cmd", "/tmp/trimui_music/vfb_osd",
						"/tmp/trimui_music/widget_ready", "/tmp/trimui_music/widget.lock"},
	[OSD_MODE_MASTER] = {"/tmp/trimui_music/master_widget_cmd", "/tmp/trimui_music/master_vfb_osd",
						 "/tmp/trimui_music/master_widget_ready", "/tmp/trimui_music/master_widget.lock"},
	[OSD_MODE_BALANCE] = {"/tmp/trimui_music/balance_widget_cmd", "/tmp/trimui_music/balance_vfb_osd",
						  "/tmp/trimui_music/balance_widget_ready", "/tmp/trimui_music/balance_widget.lock"},
	[OSD_MODE_BRIGHTNESS] = {"/tmp/trimui_music/brightness_widget_cmd", "/tmp/trimui_music/brightness_vfb_osd",
							 "/tmp/trimui_music/brightness_widget_ready", "/tmp/trimui_music/brightness_widget.lock"}};

static volatile sig_atomic_t quit;

typedef struct {
	OSDMode mode;
	const OSDPaths* paths;
	int cmd_fd;
	int lock_fd;
	void* pixels;
	SDL_Surface* render_surface;
	TTF_Font* font;
	bool enabled;
	bool active;
	bool connected;
	MusicSnapshotWire snapshot;
	int focus;
	char input[OSD_INPUT_LIMIT];
	size_t input_length;
} OSDAdapter;

static void on_signal(int signal_number) {
	(void)signal_number;
	quit = 1;
}

static void clear_ready(const OSDAdapter* adapter) {
	unlink(adapter->paths->ready);
}

static void draw_rect(SDL_Surface* surface, int x, int y, int width, int height, Uint32 color) {
	SDL_Rect rect = {.x = x, .y = y, .w = width, .h = height};
	SDL_FillRect(surface, &rect, color);
}

static void draw_triangle(SDL_Surface* surface, int x, int y, int size, int direction, Uint32 color) {
	for (int row = 0; row < size; row++) {
		int width = row + 1;
		int start = direction < 0 ? x + size - row - 1 : x;
		for (int column = 0; column < width; column++) {
			SDL_Rect pixel = {.x = start + (direction < 0 ? column : column), .y = y + row, .w = 1, .h = 1};
			SDL_FillRect(surface, &pixel, color);
		}
	}
}

static void draw_previous(SDL_Surface* surface, int x, int y, Uint32 color) {
	draw_rect(surface, x, y, 5, 34, color);
	for (int row = 0; row < 34; row++) {
		int width = row < 17 ? row * 2 : (33 - row) * 2;
		if (width > 30)
			width = 30;
		draw_rect(surface, x + 39 - width, y + row, width, 1, color);
	}
}

static void draw_next(SDL_Surface* surface, int x, int y, Uint32 color) {
	for (int row = 0; row < 34; row++) {
		int width = row < 17 ? row * 2 : (33 - row) * 2;
		if (width > 30)
			width = 30;
		draw_rect(surface, x, y + row, width, 1, color);
	}
	draw_rect(surface, x + 34, y, 5, 34, color);
}

static void draw_play(SDL_Surface* surface, int x, int y, Uint32 color) {
	for (int row = 0; row < 36; row++) {
		int width = row < 18 ? row * 2 : (35 - row) * 2;
		if (width > 32)
			width = 32;
		draw_rect(surface, x, y + row, width, 1, color);
	}
}

static void draw_pause(SDL_Surface* surface, int x, int y, Uint32 color) {
	draw_rect(surface, x, y, 9, 35, color);
	draw_rect(surface, x + 18, y, 9, 35, color);
}

static void draw_text(SDL_Surface* surface, TTF_Font* font, const char* text, int x, int y,
					  SDL_Color color, int max_width) {
	if (!font || !text || !text[0])
		return;
	char shortened[128];
	strncpy(shortened, text, sizeof(shortened) - 1);
	shortened[sizeof(shortened) - 1] = '\0';
	SDL_Surface* rendered = TTF_RenderUTF8_Blended(font, shortened, color);
	if (!rendered)
		return;
	if (rendered->w > max_width) {
		SDL_FreeSurface(rendered);
		for (size_t length = strlen(shortened); length > 3; length--) {
			shortened[length - 3] = '.';
			shortened[length - 2] = '.';
			shortened[length - 1] = '.';
			shortened[length] = '\0';
			rendered = TTF_RenderUTF8_Blended(font, shortened, color);
			if (rendered && rendered->w <= max_width)
				break;
			if (rendered)
				SDL_FreeSurface(rendered);
			rendered = NULL;
		}
	}
	if (rendered) {
		SDL_Rect destination = {.x = x, .y = y, .w = rendered->w, .h = rendered->h};
		SDL_SetSurfaceBlendMode(rendered, SDL_BLENDMODE_BLEND);
		SDL_BlitSurface(rendered, NULL, surface, &destination);
		SDL_FreeSurface(rendered);
	}
}

static const char* snapshot_state(const OSDAdapter* adapter) {
	if (!adapter->connected)
		return "Music unavailable";
	if (adapter->snapshot.source == MUSIC_SOURCE_RADIO) {
		switch (adapter->snapshot.source_state) {
		case MUSIC_RADIO_CONNECTING:
			return "Radio connecting";
		case MUSIC_RADIO_BUFFERING:
			return "Radio buffering";
		case MUSIC_RADIO_PLAYING:
			return adapter->snapshot.state == MUSIC_STATE_PLAYING ? "Playing" : "Paused";
		case MUSIC_RADIO_ERROR:
			return "Radio error";
		default:
			return "Radio stopped";
		}
	}
	if (adapter->snapshot.source == MUSIC_SOURCE_PODCAST && adapter->snapshot.state == MUSIC_STATE_PLAYING)
		return "Podcast playing";
	if (adapter->snapshot.source != MUSIC_SOURCE_NONE && adapter->snapshot.source != MUSIC_SOURCE_LOCAL &&
		adapter->snapshot.source != MUSIC_SOURCE_RADIO && adapter->snapshot.source != MUSIC_SOURCE_PODCAST)
		return "Source unavailable";
	switch (adapter->snapshot.state) {
	case MUSIC_STATE_PLAYING:
		return "Playing";
	case MUSIC_STATE_PAUSED:
		return "Paused";
	case MUSIC_STATE_STOPPED:
		return adapter->snapshot.source == MUSIC_SOURCE_NONE ? "No music" : "Stopped";
	default:
		return "Music unavailable";
	}
}

static const char* snapshot_title(const MusicSnapshotWire* snapshot) {
	if (snapshot->title[0])
		return snapshot->title;
	if (snapshot->current_file[0]) {
		const char* slash = strrchr(snapshot->current_file, '/');
		return slash ? slash + 1 : snapshot->current_file;
	}
	return "No track selected";
}

static const char* snapshot_artist(const MusicSnapshotWire* snapshot) {
	if (snapshot->artist[0])
		return snapshot->artist;
	if (snapshot->source == MUSIC_SOURCE_RADIO)
		return "Radio";
	if (snapshot->source == MUSIC_SOURCE_PODCAST)
		return "Podcast";
	return "";
}

static bool controls_available(const OSDAdapter* adapter) {
	bool supported_source = adapter->snapshot.source == MUSIC_SOURCE_LOCAL ||
							adapter->snapshot.source == MUSIC_SOURCE_RADIO || adapter->snapshot.source == MUSIC_SOURCE_PODCAST;
	return adapter->connected && supported_source &&
		   (adapter->snapshot.capabilities & (MUSIC_CAP_PLAY | MUSIC_CAP_PAUSE | MUSIC_CAP_NEXT | MUSIC_CAP_PREVIOUS));
}

static void render_music(OSDAdapter* adapter) {
	if (!adapter->render_surface)
		return;
	SDL_Surface* surface = adapter->render_surface;
	Uint32 transparent = SDL_MapRGBA(surface->format, 0, 0, 0, 0);
	Uint32 panel = SDL_MapRGBA(surface->format, 42, 42, 42, 238);
	Uint32 white = SDL_MapRGBA(surface->format, 245, 245, 245, 255);
	Uint32 ink = SDL_MapRGBA(surface->format, 42, 42, 42, 255);
	Uint32 disabled = SDL_MapRGBA(surface->format, 90, 90, 90, 255);
	SDL_FillRect(surface, NULL, transparent);
	draw_rect(surface, 0, 0, OSD_WIDTH, OSD_HEIGHT, panel);
	if (adapter->focus == 0)
		draw_rect(surface, 5, 70, 70, 45, white);
	else if (adapter->focus == 1)
		draw_rect(surface, 95, 70, 70, 45, white);
	else
		draw_rect(surface, 185, 70, 70, 45, white);

	SDL_Color text = {245, 245, 245, 255};
	SDL_Color secondary = {190, 190, 190, 255};
	draw_text(surface, adapter->font, snapshot_title(&adapter->snapshot), 10, 4, text, 240);
	draw_text(surface, adapter->font, snapshot_artist(&adapter->snapshot), 10, 26, secondary, 240);
	draw_text(surface, adapter->font, snapshot_state(adapter), 10, 48, secondary, 240);

	bool available = controls_available(adapter);
	Uint32 previous_color = available && (adapter->snapshot.capabilities & MUSIC_CAP_PREVIOUS)
								? (adapter->focus == 0 ? ink : white)
								: disabled;
	Uint32 next_color = available && (adapter->snapshot.capabilities & MUSIC_CAP_NEXT)
							? (adapter->focus == 2 ? ink : white)
							: disabled;
	bool transport_action_available = adapter->snapshot.state == MUSIC_STATE_PLAYING
										  ? (adapter->snapshot.capabilities & MUSIC_CAP_PAUSE) != 0
										  : (adapter->snapshot.capabilities & MUSIC_CAP_PLAY) != 0;
	Uint32 play_color = available && transport_action_available
							? (adapter->focus == 1 ? ink : white)
							: disabled;
	draw_previous(surface, 15, 76, previous_color);
	if (adapter->snapshot.state == MUSIC_STATE_PLAYING)
		draw_pause(surface, 116, 77, play_color);
	else
		draw_play(surface, 116, 77, play_color);
	draw_next(surface, 198, 76, next_color);
}

static int clamp_slider_value(int value) {
	if (value < 0)
		return 0;
	if (value > OSD_SLIDER_MAX)
		return OSD_SLIDER_MAX;
	return value;
}

static int balance_value(void) {
	int game = GetGameVolume();
	int music = GetMusicVolume();
	if (game < 0)
		game = 0;
	if (game > 20)
		game = 20;
	if (music < 0)
		music = 0;
	if (music > 20)
		music = 20;
	int stronger = game > music ? game : music;
	if (stronger == 0)
		return 5;
	int offset = (music - game) * (OSD_SLIDER_MAX / 2);
	if (offset >= 0)
		offset = (offset + stronger / 2) / stronger;
	else
		offset = (offset - stronger / 2) / stronger;
	return clamp_slider_value(OSD_SLIDER_MAX / 2 + offset);
}

static int slider_value(const OSDAdapter* adapter) {
	switch (adapter->mode) {
	case OSD_MODE_MASTER:
		return clamp_slider_value((GetVolume() + OSD_VOLUME_STEP / 2) / OSD_VOLUME_STEP);
	case OSD_MODE_BALANCE:
		return balance_value();
	case OSD_MODE_BRIGHTNESS:
		return clamp_slider_value(GetBrightness());
	case OSD_MODE_MUSIC:
	default:
		return 0;
	}
}

static const char* slider_label(const OSDAdapter* adapter) {
	switch (adapter->mode) {
	case OSD_MODE_MASTER:
		return "Master";
	case OSD_MODE_BALANCE:
		return "Balance";
	case OSD_MODE_BRIGHTNESS:
		return "Brightness";
	case OSD_MODE_MUSIC:
	default:
		return "";
	}
}

static void set_balance_value(int value) {
	int music = value <= OSD_SLIDER_MAX / 2 ? value * OSD_BALANCE_SIDE_STEP : 20;
	int game = value <= OSD_SLIDER_MAX / 2 ? 20 : (OSD_SLIDER_MAX - value) * OSD_BALANCE_SIDE_STEP;
	if (MusicClient_setVolume(music) != MUSIC_STATUS_OK)
		return;
	SetGameVolume(game);
}

static void set_slider_value(const OSDAdapter* adapter, int value) {
	switch (adapter->mode) {
	case OSD_MODE_MASTER:
		SetVolume(value * OSD_VOLUME_STEP);
		break;
	case OSD_MODE_BALANCE:
		set_balance_value(value);
		break;
	case OSD_MODE_BRIGHTNESS:
		SetBrightness(value);
		break;
	case OSD_MODE_MUSIC:
	default:
		break;
	}
}

static void adjust_slider(OSDAdapter* adapter, int delta) {
	int value = slider_value(adapter) + delta;
	int maximum = OSD_SLIDER_MAX;
	if (value < 0)
		value = 0;
	if (value > maximum)
		value = maximum;
	set_slider_value(adapter, value);
}

static void render_slider(OSDAdapter* adapter) {
	if (!adapter->render_surface)
		return;
	SDL_Surface* surface = adapter->render_surface;
	Uint32 transparent = SDL_MapRGBA(surface->format, 0, 0, 0, 0);
	Uint32 panel = SDL_MapRGBA(surface->format, 42, 42, 42, 238);
	Uint32 track = SDL_MapRGBA(surface->format, 82, 82, 82, 255);
	Uint32 white = SDL_MapRGBA(surface->format, 245, 245, 245, 255);
	Uint32 muted = SDL_MapRGBA(surface->format, 160, 160, 160, 255);
	SDL_Color text = {245, 245, 245, 255};
	SDL_Color secondary = {190, 190, 190, 255};
	int value = slider_value(adapter);
	int maximum = OSD_SLIDER_MAX;
	int bar_x = 12;
	int bar_y = 55;
	int bar_width = OSD_WIDTH - 24;
	int fill_width = maximum > 0 ? (bar_width * value) / maximum : 0;

	SDL_FillRect(surface, NULL, transparent);
	draw_rect(surface, 0, 0, OSD_WIDTH, OSD_HEIGHT, panel);
	draw_text(surface, adapter->font, slider_label(adapter), 12, 12, text, 150);
	char value_text[32];
	if (adapter->mode == OSD_MODE_BALANCE) {
		int offset = value - maximum / 2;
		if (offset == 0)
			snprintf(value_text, sizeof(value_text), "50/50");
		else
			snprintf(value_text, sizeof(value_text), "%s +%d", offset < 0 ? "Game" : "Music", abs(offset));
	} else {
		snprintf(value_text, sizeof(value_text), "%d / %d", value, maximum);
	}
	draw_text(surface, adapter->font, value_text, 170, 12, secondary, 78);
	draw_rect(surface, bar_x, bar_y, bar_width, 18, track);
	if (adapter->mode == OSD_MODE_BALANCE) {
		int center = bar_width / 2;
		int start = fill_width < center ? fill_width : center;
		draw_rect(surface, bar_x + start, bar_y, abs(fill_width - center), 18, white);
		draw_rect(surface, bar_x + fill_width - 3, bar_y - 3, 6, 24, white);
	} else if (fill_width > 0)
		draw_rect(surface, bar_x, bar_y, fill_width, 18, white);
	if (adapter->mode == OSD_MODE_BALANCE) {
		draw_rect(surface, bar_x + bar_width / 2 - 1, bar_y - 4, 2, 26, white);
		draw_text(surface, adapter->font, "Game", 12, 78, secondary, 70);
		draw_text(surface, adapter->font, "Music", 204, 78, secondary, 54);
	}
	if (adapter->active)
		draw_rect(surface, bar_x, bar_y + 18, bar_width, 3, white);
	else
		draw_rect(surface, bar_x, bar_y + 18, bar_width, 3, muted);
	draw_text(surface, adapter->font, adapter->active ? "Left / Right" : "A adjust", 12, 99, secondary, 200);
}

static void publish_frame(OSDAdapter* adapter) {
	if (!adapter->render_surface || !adapter->pixels)
		return;

	const size_t row_bytes = OSD_WIDTH * 4;
	const Uint8* source = adapter->render_surface->pixels;
	Uint8* destination = adapter->pixels;
	bool changed = false;
	if (adapter->render_surface->pitch == (int)row_bytes) {
		changed = memcmp(source, destination, OSD_BYTES) != 0;
	} else {
		for (int row = 0; row < OSD_HEIGHT; row++) {
			if (memcmp(source + row * adapter->render_surface->pitch, destination + row * row_bytes, row_bytes) != 0) {
				changed = true;
				break;
			}
		}
	}
	if (!changed)
		return;

	if (adapter->render_surface->pitch == (int)row_bytes) {
		memcpy(destination, source, OSD_BYTES);
	} else {
		for (int row = 0; row < OSD_HEIGHT; row++)
			memcpy(destination + row * row_bytes, source + row * adapter->render_surface->pitch, row_bytes);
	}
}

static void render(OSDAdapter* adapter) {
	if (adapter->mode == OSD_MODE_MUSIC)
		render_music(adapter);
	else
		render_slider(adapter);
	publish_frame(adapter);
}

static int open_fifo(const OSDAdapter* adapter) {
	struct stat info;
	if (lstat(adapter->paths->command, &info) == 0 && !S_ISFIFO(info.st_mode)) {
		if (unlink(adapter->paths->command) != 0)
			return -1;
	}
	if (mkfifo(adapter->paths->command, 0x180) != 0 && errno != EEXIST)
		return -1;
	return open(adapter->paths->command, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
}

static int open_canvas(OSDAdapter* adapter) {
	int fd = open(adapter->paths->canvas, O_RDWR | O_CREAT, 0x180);
	if (fd < 0)
		return -1;
	if (ftruncate(fd, OSD_BYTES) != 0) {
		close(fd);
		return -1;
	}
	adapter->pixels = mmap(NULL, OSD_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (adapter->pixels == MAP_FAILED) {
		adapter->pixels = NULL;
		return -1;
	}
	adapter->render_surface = SDL_CreateRGBSurface(0, OSD_WIDTH, OSD_HEIGHT, 32,
												   0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
	return adapter->render_surface ? 0 : -1;
}

static int write_ready(const OSDAdapter* adapter) {
	int fd = open(adapter->paths->ready, O_WRONLY | O_CREAT | O_TRUNC, 0x180);
	if (fd < 0)
		return -1;
	char pid[32];
	int length = snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
	int result = write(fd, pid, (size_t)length) == length ? 0 : -1;
	close(fd);
	return result;
}

static bool uses_music_client(const OSDAdapter* adapter) {
	return adapter->mode == OSD_MODE_MUSIC || adapter->mode == OSD_MODE_BALANCE;
}

static void reconnect_snapshot(OSDAdapter* adapter) {
	if (!uses_music_client(adapter))
		return;
	MusicClient_update();
	adapter->snapshot = *MusicClient_snapshot();
	adapter->connected = MusicClient_isConnected();
}

static void action(OSDAdapter* adapter, uint16_t command) {
	switch (command) {
	case MUSIC_CMD_PREVIOUS:
		(void)MusicClient_previous();
		break;
	case MUSIC_CMD_TOGGLE:
		(void)MusicClient_toggle();
		break;
	case MUSIC_CMD_NEXT:
		(void)MusicClient_next();
		break;
	default:
		return;
	}
	reconnect_snapshot(adapter);
}

static void process_command(OSDAdapter* adapter, const char* command) {
	if (!strcmp(command, "enable")) {
		adapter->enabled = true;
		adapter->active = adapter->mode != OSD_MODE_MUSIC;
		return;
	}
	if (!strcmp(command, "disable") || !strcmp(command, "key_b")) {
		adapter->enabled = false;
		adapter->active = false;
		return;
	}
	if (!adapter->enabled)
		return;
	if (adapter->mode != OSD_MODE_MUSIC) {
		if (!strcmp(command, "key_a"))
			adapter->active = true;
		else if (adapter->active && !strcmp(command, "left"))
			adjust_slider(adapter, -1);
		else if (adapter->active && !strcmp(command, "right"))
			adjust_slider(adapter, 1);
		return;
	}
	if (!strcmp(command, "left")) {
		adapter->focus = (adapter->focus + 2) % 3;
	} else if (!strcmp(command, "right")) {
		adapter->focus = (adapter->focus + 1) % 3;
	} else if (!strcmp(command, "key_a")) {
		if (adapter->focus == 0)
			action(adapter, MUSIC_CMD_PREVIOUS);
		else if (adapter->focus == 1)
			action(adapter, MUSIC_CMD_TOGGLE);
		else
			action(adapter, MUSIC_CMD_NEXT);
	}
}

static void process_input(OSDAdapter* adapter) {
	for (;;) {
		char* newline = memchr(adapter->input, '\n', adapter->input_length);
		if (!newline)
			return;
		*newline = '\0';
		process_command(adapter, adapter->input);
		size_t consumed = (size_t)(newline - adapter->input) + 1;
		memmove(adapter->input, adapter->input + consumed, adapter->input_length - consumed);
		adapter->input_length -= consumed;
	}
}

static void read_commands(OSDAdapter* adapter) {
	for (;;) {
		if (adapter->input_length == sizeof(adapter->input) - 1) {
			adapter->input_length = 0;
		}
		ssize_t length = read(adapter->cmd_fd, adapter->input + adapter->input_length,
							  sizeof(adapter->input) - 1 - adapter->input_length);
		if (length > 0) {
			adapter->input_length += (size_t)length;
			adapter->input[adapter->input_length] = '\0';
			process_input(adapter);
			continue;
		}
		if (length < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		if (length == 0 || (length < 0 && errno != EINTR)) {
			close(adapter->cmd_fd);
			adapter->cmd_fd = open_fifo(adapter);
			adapter->input_length = 0;
		}
		return;
	}
}

static bool parse_mode(const char* name, OSDMode* mode) {
	if (!name || !strcmp(name, "music")) {
		*mode = OSD_MODE_MUSIC;
		return true;
	}
	if (!strcmp(name, "master")) {
		*mode = OSD_MODE_MASTER;
		return true;
	}
	if (!strcmp(name, "balance")) {
		*mode = OSD_MODE_BALANCE;
		return true;
	}
	if (!strcmp(name, "brightness")) {
		*mode = OSD_MODE_BRIGHTNESS;
		return true;
	}
	return false;
}

int main(int argc, char** argv) {
	OSDMode mode;
	if (argc > 2 || !parse_mode(argc == 2 ? argv[1] : NULL, &mode))
		return 2;
	OSDAdapter adapter;
	memset(&adapter, 0, sizeof(adapter));
	adapter.mode = mode;
	adapter.paths = &OSD_PATHS[mode];
	adapter.cmd_fd = -1;
	adapter.lock_fd = -1;
	adapter.focus = 1;
	adapter.enabled = true;
	adapter.active = false;
	mkdir("/tmp/trimui_music", 0x1c0);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	adapter.lock_fd = open(adapter.paths->lock, O_RDWR | O_CREAT, 0x180);
	if (adapter.lock_fd < 0 || flock(adapter.lock_fd, LOCK_EX | LOCK_NB) != 0)
		return 0;
	clear_ready(&adapter);
	InitSettings();
	if (uses_music_client(&adapter) && MusicClient_init("/mnt/SDCARD/.system/bin/musicplayerd.elf") != 0)
		adapter.connected = false;
	if (SDL_Init(0) != 0)
		goto cleanup;
	if (TTF_Init() != 0)
		goto cleanup;
	if (open_canvas(&adapter) != 0)
		goto cleanup;
	adapter.cmd_fd = open_fifo(&adapter);
	if (adapter.cmd_fd < 0)
		goto cleanup;
	adapter.font = TTF_OpenFont(OSD_FONT_PATH, 16);
	if (!adapter.font)
		goto cleanup;
	reconnect_snapshot(&adapter);
	render(&adapter);
	if (write_ready(&adapter) != 0)
		goto cleanup;
	int64_t next_refresh = 0;
	while (!quit) {
		if (adapter.cmd_fd < 0) {
			adapter.cmd_fd = open_fifo(&adapter);
			usleep(OSD_POLL_MS * 1000);
		} else {
			struct pollfd poll_fd = {.fd = adapter.cmd_fd, .events = POLLIN};
			(void)poll(&poll_fd, 1, OSD_POLL_MS);
			if (poll_fd.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))
				read_commands(&adapter);
		}
		int64_t now = (int64_t)SDL_GetTicks();
		if (now >= next_refresh) {
			next_refresh = now + OSD_REFRESH_MS;
			reconnect_snapshot(&adapter);
			render(&adapter);
		}
	}

cleanup:
	clear_ready(&adapter);
	if (adapter.font)
		TTF_CloseFont(adapter.font);
	if (adapter.render_surface)
		SDL_FreeSurface(adapter.render_surface);
	if (adapter.pixels)
		munmap(adapter.pixels, OSD_BYTES);
	if (adapter.cmd_fd >= 0)
		close(adapter.cmd_fd);
	if (TTF_WasInit())
		TTF_Quit();
	SDL_Quit();
	if (uses_music_client(&adapter))
		MusicClient_quit();
	QuitSettings();
	if (adapter.lock_fd >= 0)
		close(adapter.lock_fd);
	return 0;
}
