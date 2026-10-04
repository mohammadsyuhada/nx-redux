#ifndef __RA_INTEGRATION_H__
#define __RA_INTEGRATION_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "ra_offline.h" // for RA_UNKNOWN_EMULATOR_ACHIEVEMENT_ID

// See: https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration

/**
 * Initialize the RetroAchievements client.
 * Should be called once at startup, after config is loaded.
 * Does nothing if RA is disabled in settings.
 */
void RA_init(void);

/**
 * Shut down the RetroAchievements client.
 * Should be called at shutdown before exiting.
 */
void RA_quit(void);

/**
 * Load a game for achievement tracking.
 * Should be called after a game ROM is loaded and the core is initialized.
 * 
 * @param rom_path Path to the ROM file
 * @param rom_data Pointer to ROM data in memory (can be NULL if core loads from file)
 * @param rom_size Size of ROM data in bytes
 * @param emu_tag The emulator tag (e.g., "GB", "SFC", "PS") for console identification
 */
void RA_loadGame(const char* rom_path, const uint8_t* rom_data, size_t rom_size, const char* emu_tag);

/**
 * Path to record in the offline cache's rom.txt for the next RA_loadGame (the
 * launcher's ROM path). Needed when RA_loadGame gets an archive's extracted tmp
 * copy; when unset, rom.txt records RA_loadGame's rom_path.
 */
void RA_setRecordedRomPath(const char* path);

/**
 * Unload the current game from achievement tracking.
 * Should be called when a game is closed/unloaded.
 */
void RA_unloadGame(void);

/**
 * Process achievements for the current frame.
 * Should be called once per frame after core.run() completes.
 */
void RA_doFrame(void);

/**
 * Process the periodic queue (for async operations).
 * Should be called when emulation is paused but we still want to process
 * server responses and other async operations.
 */
void RA_idle(void);

/**
 * The in-game menu just opened: one more login attempt if the launch-time
 * retries gave up, or a background upload of journaled unlocks if the
 * session is offline and WiFi is connected. Nothing polls during play.
 */
void RA_onMenuOpen(void);

/**
 * Check if a game is currently loaded and being tracked.
 * @return true if a game is loaded and RA is active
 */
bool RA_isGameLoaded(void);

/**
 * Check if hardcore mode is currently active.
 * Use this to block save states, cheats, etc.
 * @return true if hardcore mode is active
 */
bool RA_isHardcoreModeActive(void);

/**
 * Get achievement summary for current game.
 * @param unlocked Output: number of achievements unlocked
 * @param total Output: total number of achievements
 */
void RA_getAchievementSummary(uint32_t* unlocked, uint32_t* total);

/**
 * Get the achievement list for the current game.
 * @param category Achievement category (RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, etc.)
 * @param grouping List grouping (RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE, etc.)
 * @return Allocated achievement list, or NULL if no game loaded. Must be freed with RA_destroyAchievementList.
 */
const void* RA_createAchievementList(int category, int grouping);

/**
 * Destroy an achievement list created by RA_createAchievementList.
 * @param list The list to destroy
 */
void RA_destroyAchievementList(const void* list);

/**
 * Check if an achievement is muted.
 * @param achievement_id The achievement ID to check
 * @return true if muted
 */
bool RA_isAchievementMuted(uint32_t achievement_id);

/**
 * Toggle the mute state of an achievement.
 * @param achievement_id The achievement ID to toggle
 * @return New mute state (true = muted)
 */
bool RA_toggleAchievementMute(uint32_t achievement_id);

/**
 * Typedef for memory read function pointer.
 * This allows minarch to provide its memory access function.
 */
typedef void* (*RA_GetMemoryFunc)(unsigned id);
typedef size_t (*RA_GetMemorySizeFunc)(unsigned id);

/**
 * Set the memory access functions.
 * These should point to the libretro core's memory access functions.
 * Must be called before RA_loadGame().
 * 
 * @param get_data Function to get memory pointer (core.get_memory_data)
 * @param get_size Function to get memory size (core.get_memory_size)
 */
void RA_setMemoryAccessors(RA_GetMemoryFunc get_data, RA_GetMemorySizeFunc get_size);

/**
 * Set the memory map from the libretro core.
 * Some cores (e.g., NES, SNES) use RETRO_ENVIRONMENT_SET_MEMORY_MAPS
 * instead of simple retro_get_memory_data/size calls.
 * 
 * @param mmap Pointer to the retro_memory_map structure (can be NULL to clear)
 */
void RA_setMemoryMap(const void* mmap);

#endif // __RA_INTEGRATION_H__
