#ifndef __RATOOLS_DATA_H__
#define __RATOOLS_DATA_H__

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum {
	RAT_ACH_LOCKED = 0,
	RAT_ACH_UNLOCKED, // on the server
	RAT_ACH_PENDING,  // unlocked offline, awaiting sync
} RAT_AchState;

typedef struct {
	uint32_t id;
	uint32_t points;
	char title[128];
	char description[256];
	char badge_name[32];
	RAT_AchState state;
	uint32_t type;      // RC_ACHIEVEMENT_TYPE_* (standard/missable/progression/win)
	float rarity;       // % of players who unlocked it (0 = unknown)
	time_t unlock_time; // server or journal unlock time; 0 while locked
} RAT_Achievement;

typedef struct {
	char hash[64];
	uint32_t session_game_id;
	char title[128];
	int total;
	int unlocked; // server unlocks + pending
	int pending;
} RAT_Game;

/** Scan cache/games/, parse each sets.json. Returns count; *out_games is
 *  malloc'd (caller frees), sorted by title. 0 with *out_games=NULL if none. */
int RAT_listGames(RAT_Game** out_games);

/** Achievements for one game with unlock state resolved from the cached
 *  session + journal. Returns count; *out malloc'd (caller frees). */
int RAT_loadAchievements(const RAT_Game* game, RAT_Achievement** out);

/** Sort achievements per the "Achievement sort order" setting
 *  (CFG_getRAAchievementSortOrder), mirroring the in-game menu's ordering. */
void RAT_sortAchievements(RAT_Achievement* achs, int count);

/** Score fields from cache/login.json. Returns false if no cache. */
bool RAT_getCachedScore(uint32_t* score, uint32_t* softcore_score);

#endif
