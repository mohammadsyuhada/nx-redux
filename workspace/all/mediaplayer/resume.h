#ifndef __RESUME_H__
#define __RESUME_H__

#include <stdbool.h>

// Resume source types for video player
typedef enum {
	RESUME_TYPE_NONE,
	RESUME_TYPE_LOCAL, // Local video file
	RESUME_TYPE_IPTV   // IPTV/Online TV stream
} ResumeType;

// Resume state
typedef struct {
	ResumeType type;
	char video_path[512];	 // Video file path or stream URL
	char video_name[256];	 // Display name for menu label
	char subtitle_path[512]; // Subtitle file (local videos only)
	int position_sec;		 // Playback position in seconds
} ResumeState;

// Initialize (loads from disk if available)
void Resume_init(void);

// Check if resume state is available
bool Resume_isAvailable(void);

// Get current resume state (read-only)
const ResumeState* Resume_getState(void);

// Get display label for menu (e.g. "Resume: Video Name")
const char* Resume_getLabel(void);

// Save resume state for local video
void Resume_saveLocal(const char* video_path, const char* video_name,
					  const char* subtitle_path, int position_sec);

// Save resume state for IPTV channel
void Resume_saveIPTV(const char* url, const char* channel_name);

// Clear resume state
void Resume_clear(void);

#endif
