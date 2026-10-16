#include "background.h"
#include "music_client.h"

BackgroundPlayerType Background_getActive(void) {
	/* The daemon snapshot is authoritative, including after a UI detach. */
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
