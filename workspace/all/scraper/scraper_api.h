#ifndef SCRAPER_API_H
#define SCRAPER_API_H

#include <stdbool.h>

// Media URLs extracted from ScreenScraper API response
typedef struct {
	char screenshot_url[1024];
	char boxart_url[1024];
	char wheel_url[1024];
	char game_name[256];
	bool found;
} ScraperGameInfo;

// User account info from ScreenScraper API
typedef struct {
	int requests_today;
	int max_requests_per_day;
	int threads;
	bool valid; // false if fetch failed
} ScraperUserInfo;

// Initialize the scraper API (call once at startup)
void ScraperAPI_init(void);

// Set optional user credentials for higher rate limits
// Both can be NULL or empty to use anonymous access
void ScraperAPI_setUserCredentials(const char* username, const char* password);

// Check if user credentials are configured
bool ScraperAPI_hasUserCredentials(void);

// Search for a game by filename and system ID
// Returns true if game was found, fills info struct
bool ScraperAPI_search(const char* rom_filename, int system_id, ScraperGameInfo* info);

// Download a file from URL to a local path
// Returns true on success
bool ScraperAPI_downloadFile(const char* url, const char* dest_path);

// Enforce rate limiting (call before each API request)
void ScraperAPI_rateLimit(void);

// Check if network is available
bool ScraperAPI_isOnline(void);

// Fetch user account info (quota, threads)
// Requires user credentials to be set
ScraperUserInfo ScraperAPI_fetchUserInfo(void);

#endif // SCRAPER_API_H
