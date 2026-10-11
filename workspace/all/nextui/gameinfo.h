#ifndef GAMEINFO_H
#define GAMEINFO_H

#include <stdbool.h>
#include <time.h>

typedef struct {
	bool has_time;
	time_t last_played;
	int seconds;
	bool has_ra;
	int unlocked, total;
	char next[128];
} GameInfo;

void GameInfo_init(void); // starts the worker; loads nothing yet
void GameInfo_quit(void); // stops and joins the worker
// Info for the game at `path` (a ROM, a folder game dir, or an .m3u). True and *out filled when it is
// ready; otherwise it is queued (latest request wins) and false is returned.
bool GameInfo_get(const char* path, GameInfo* out);
// The same for a game shown ahead (a neighbour's caption made between frames): a cached result, else it is queued
// behind any GameInfo_get request (the latest prefetch wins), never displacing the selection's.
bool GameInfo_prefetch(const char* path, GameInfo* out);
// True once after the worker produced a new result (main loop: redraw).
bool GameInfo_checkAsyncLoaded(void);

#endif
