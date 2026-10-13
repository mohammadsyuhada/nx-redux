#ifndef __RA_BADGES_H__
#define __RA_BADGES_H__

#include "sdl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Implements the badge download/caching that the integration guide leaves to the emulator.
// See: https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration#showing-the-game-placard

// Badge size for notifications (will be scaled)
#define RA_BADGE_NOTIFY_SIZE 24 // Size for notification icons

// Cache directory: one folder per game id in the loaded rom's console folder,
// Roms/<Folder>/.retroachievements/badges/<game_id>/, or RA_BADGE_CACHE_DIR
// (ra_paths.h)/<game_id>/ for a rom outside Roms/<Folder>/ (ra_badge_path.h).
// Shared by every account. RA_Badges_setRomPath() names the loaded rom; every
// call takes the RA game id the badge belongs to; game id 0 has no badges.
#include "ra_paths.h"

// Badge state
typedef enum {
	RA_BADGE_STATE_UNKNOWN,		// Badge not yet requested
	RA_BADGE_STATE_DOWNLOADING, // Download in progress
	RA_BADGE_STATE_CACHED,		// Downloaded and cached locally
	RA_BADGE_STATE_FAILED,		// Download failed
} RA_BadgeState;

/**
 * Initialize the badge cache system.
 * Creates cache directory if needed.
 */
void RA_Badges_init(void);

/**
 * Shutdown the badge cache system.
 * Clears any loaded surfaces (but keeps cached files).
 */
void RA_Badges_quit(void);

/**
 * Clear the in-memory badge surface cache.
 * Called when unloading a game to free memory.
 * Does not delete the on-disk cache.
 */
void RA_Badges_clearMemory(void);

/**
 * The rom the badges belong to, as the user sees it (the archive, not an
 * unpacked copy; NULL/empty: unknown, badges go to the fallback folder).
 * Set when a game loads, cleared (NULL) when it unloads. Changing it clears
 * the in-memory cache and drops queued downloads.
 */
void RA_Badges_setRomPath(const char* rom_path);

/**
 * Pre-download all badges for the current game's achievements.
 * Should be called after game load when achievement list is available.
 * Returns immediately: a worker thread checks which badges are already in
 * the game's badge folder and queues downloads for the missing ones.
 *
 * @param game_id RetroAchievements game ID (the badge folder)
 * @param badge_names Array of achievement badge names (copied)
 * @param count Number of badge names
 */
void RA_Badges_prefetch(uint32_t game_id, const char** badge_names, size_t count);

/**
 * Show/hide the "Loading achievement badges..." toast. Call once per frame
 * on the main thread (Notification_* is not thread-safe).
 */
void RA_Badges_update(void);

/**
 * Get a badge surface. Returns cached surface or NULL if not available.
 * Downloads the badge if not cached (returns NULL immediately, call again later).
 * 
 * @param game_id RetroAchievements game ID the badge belongs to (0 = NULL)
 * @param badge_name The badge name (e.g., "00234")
 * @param locked Whether to get the locked version (_lock suffix)
 * @return SDL_Surface* The badge surface, or NULL if not yet cached
 *         Caller does NOT own the surface - do not free it.
 */
SDL_Surface* RA_Badges_get(uint32_t game_id, const char* badge_name, bool locked);

/**
 * Get a badge surface scaled to notification size.
 * 
 * @param game_id RetroAchievements game ID the badge belongs to (0 = NULL)
 * @param badge_name The badge name (e.g., "00234")
 * @param locked Whether to get the locked version
 * @return SDL_Surface* The scaled badge surface, or NULL if not available
 *         Caller does NOT own the surface - do not free it.
 */
SDL_Surface* RA_Badges_getNotificationSize(uint32_t game_id, const char* badge_name, bool locked);

/**
 * Get the cache file path for a badge of the rom set by RA_Badges_setRomPath():
 * <badge folder of that rom>/<game_id>/<badge_name>[_lock].png
 * 
 * @param game_id RetroAchievements game ID the badge belongs to
 * @param badge_name The badge name
 * @param locked Whether to get the locked version path
 * @param buffer Output buffer for the path
 * @param buffer_size Size of the output buffer
 * @return false (buffer empty) for game id 0 or an unusable name
 */
bool RA_Badges_getCachePath(uint32_t game_id, const char* badge_name, bool locked, char* buffer,
							size_t buffer_size);

/**
 * Build the URL for a badge.
 * 
 * @param badge_name The badge name
 * @param locked Whether to get the locked version URL
 * @param buffer Output buffer for the URL
 * @param buffer_size Size of the output buffer
 */
void RA_Badges_getUrl(const char* badge_name, bool locked, char* buffer, size_t buffer_size);

#endif // __RA_BADGES_H__
