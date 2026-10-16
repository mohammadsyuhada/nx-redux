#ifndef __BACKGROUND_H__
#define __BACKGROUND_H__

#include <stdbool.h>

// Background player types
typedef enum {
	BG_NONE = 0,
	BG_MUSIC,
	BG_RADIO,
	BG_PODCAST
} BackgroundPlayerType;

// Get the active background player type (from the daemon snapshot)
BackgroundPlayerType Background_getActive(void);

// Stop whatever is playing in the background
void Background_stopAll(void);

// Check if any background player is active
bool Background_isPlaying(void);

// Refresh the copied daemon snapshot (the daemon owns advancement and resume saving)
void Background_tick(void);

#endif
