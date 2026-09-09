#include "background.h"
#include "music_client.h"

static BackgroundPlayerType active_bg = BG_NONE;

void Background_setActive(BackgroundPlayerType type) {
	active_bg = type;
}

BackgroundPlayerType Background_getActive(void) {
	/* The owner is authoritative after a UI detach; active_bg is only a
	 * presentation hint and may describe the page that was closed. */
	const MusicSnapshotWire* snapshot = MusicClient_snapshot();
	switch (snapshot->source) {
	case MUSIC_SOURCE_LOCAL:
		return BG_MUSIC;
	case MUSIC_SOURCE_RADIO:
		return BG_RADIO;
	case MUSIC_SOURCE_PODCAST:
		return BG_PODCAST;
	default:
		return BG_NONE;
	}
}

void Background_stopAll(void) {
	(void)MusicClient_stop();
	active_bg = BG_NONE;
}

bool Background_isPlaying(void) {
	const MusicSnapshotWire* snapshot = MusicClient_snapshot();
	return snapshot->source != MUSIC_SOURCE_NONE && snapshot->state != MUSIC_STATE_STOPPED;
}

void Background_tick(void) {
	// Playback, EOF/queue advancement, HID, and source progress belong to the
	// daemon. The UI only refreshes its copied snapshot.
	MusicClient_update();
}
