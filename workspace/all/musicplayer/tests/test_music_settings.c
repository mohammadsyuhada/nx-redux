// Host-compiled unit test for settings.c (no device toolchain).
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I. -I../../tg5040/platform \
//      -DMUSIC_SETTINGS_TEST_DIR='"/tmp/nx_music_settings_roundtrip"' \
//      settings.c tests/test_music_settings.c -o /tmp/test_music_settings -lm && /tmp/test_music_settings
//
// Persists every field the public settings.h API exposes to a non-default value,
// reloads from disk into fresh in-memory state and checks each survived, then
// checks a missing file yields the documented defaults. The UI settings
// (screen-off, lyrics) and the owner settings (audio pipeline) are written by
// their respective owner modes, exactly as musicplayer and musicplayerd do.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../settings.h"

#ifndef MUSIC_SETTINGS_TEST_DIR
#error "define MUSIC_SETTINGS_TEST_DIR to a writable temp directory"
#endif

#define SETTINGS_FILE MUSIC_SETTINGS_TEST_DIR "/settings.cfg"
#define SETTINGS_LOCK MUSIC_SETTINGS_TEST_DIR "/settings.cfg.lock"

static int failures;
static void check(int condition, const char* message) {
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}
static int near(float a, float b) {
	return fabsf(a - b) < 0.001f;
}

int main(void) {
	mkdir(MUSIC_SETTINGS_TEST_DIR, 0755);
	unlink(SETTINGS_FILE);
	unlink(SETTINGS_LOCK);

	// --- Write the non-default values through the public API ---------------
	// UI owner persists the screen-off timeout and the lyrics toggle.
	Settings_setOwnerMode(false);
	Settings_init();
	Settings_cycleScreenOffNext(); // 60 -> 90 (default is 60)
	Settings_toggleLyrics();	   // true -> false (default is true)

	// Audio owner persists the five pipeline settings; it preserves the UI
	// lines it finds in the file, so the round trip keeps all seven.
	Settings_setOwnerMode(true);
	Settings_init();			   // reloads the UI lines, audio still at defaults
	Settings_setAudioValues(200,   // bass_filter_hz  (default 120)
							0.0f,  // soft limiter Off (default Medium/0.6)
							1,	   // follow source   (default 0)
							2,	   // resampler Best   (default 0/Fast)
							4096); // buffer frames    (default 2048)

	// --- Reload into fresh in-memory state and verify every field ----------
	Settings_init();
	check(Settings_getScreenOffTimeout() == 90, "screen-off timeout round trips");
	check(Settings_getLyricsEnabled() == false, "lyrics toggle round trips");
	check(Settings_getBassFilterHz() == 200, "bass filter round trips");
	check(near(Settings_getSoftLimiterThreshold(), 0.0f), "soft limiter round trips");
	check(Settings_getRateModeFollowSource() == 1, "rate-follow round trips");
	check(Settings_getResamplerQuality() == 2, "resampler quality round trips");
	check(Settings_getBufferFrames() == 4096, "buffer frames round trips");

	// --- A missing file yields the documented defaults ---------------------
	unlink(SETTINGS_FILE);
	unlink(SETTINGS_LOCK);
	Settings_init();
	check(Settings_getScreenOffTimeout() == 60, "default screen-off timeout is 60s");
	check(Settings_getLyricsEnabled() == true, "lyrics default on");
	check(Settings_getBassFilterHz() == 120, "default bass filter is 120 Hz");
	check(near(Settings_getSoftLimiterThreshold(), 0.6f), "default soft limiter is Medium");
	check(Settings_getRateModeFollowSource() == 0, "default rate mode is device default");
	check(Settings_getResamplerQuality() == 0, "default resampler quality is Fast");
	check(Settings_getBufferFrames() == 2048, "default buffer frames is 2048");

	unlink(SETTINGS_FILE);
	unlink(SETTINGS_LOCK);
	rmdir(MUSIC_SETTINGS_TEST_DIR);
	if (!failures)
		puts("music settings round-trip test passed");
	return failures ? 1 : 0;
}
