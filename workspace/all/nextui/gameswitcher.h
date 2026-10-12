#ifndef GAMESWITCHER_H
#define GAMESWITCHER_H

#include "sdl.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
	bool dirty;
	bool folderbgchanged;
	bool startgame;
	int screen;
	int gsanimdir;
} GameSwitcherResult;

struct Directory;

void GameSwitcher_init(void);
int GameSwitcher_shouldStartInSwitcher(void);
void GameSwitcher_resetSelection(void);
GameSwitcherResult GameSwitcher_handleInput(unsigned long now);
// Full screen: the selected game's resume frame with its title, console and info line over it. status_w is the
// menu bar's status group width (the title keeps clear of it).
void GameSwitcher_render(int lastScreen, SDL_Surface* blackBG, int gsanimdir, int status_w);
// The bottom of the Full screen header (the console line under the title): where its top fade is measured from.
int GameSwitcher_headerBottom(void);

// Layouts > Game Switcher layout: List. nextui.c says each frame whether the switcher is the screen shown; while it is
// and the layout is List with games in it, the switcher is a game list's List (gamelist.c draws it over
// GameSwitcher_listDir, as Home's List over Home_listDir) and the rest of the launcher reads as it would under a game
// list (Home, Grid and the rows inactive).
void GameSwitcher_setShown(bool shown);
bool GameSwitcher_isList(void);
struct Directory* GameSwitcher_listDir(void);					// NULL unless GameSwitcher_isList(); valid until the next rebuild
void GameSwitcher_readyList(void);								// readyResume for the selected game: once a frame, before the List draws
void GameSwitcher_listArtPath(char* out, size_t size);			// the selected game's resume frame, else its screenshot
bool GameSwitcher_listArtPathAt(int i, char* out, size_t size); // row i's (a neighbour's, for the List's look ahead)
void GameSwitcher_renderListHints(SDL_Surface* dst);			// B BACK, Y REMOVE, A RESUME (Button hints shown)

#endif // GAMESWITCHER_H
