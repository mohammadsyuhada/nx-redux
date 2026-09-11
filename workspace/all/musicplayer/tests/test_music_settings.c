#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../settings.h"

void InitSettings(void) {
}

static int write_ui_settings(void) {
	Settings_setOwnerMode(false);
	Settings_init();
	for (int i = 0; i < 100; i++)
		Settings_cycleScreenOffNext();
	return 0;
}

static int write_owner_settings(void) {
	Settings_setOwnerMode(true);
	Settings_init();
	for (int i = 0; i < 100; i++)
		Settings_setAudioValues(i & 1 ? 80 : 120, 0.6f, i & 1, i % 3, i & 1 ? 1024 : 4096);
	return 0;
}

int main(void) {
	const char* dir = "/tmp/nx_music_settings_test";
	const char* file = "/tmp/nx_music_settings_test/settings.cfg";
	mkdir(dir, 493);
	unlink(file);
	unlink("/tmp/nx_music_settings_test/settings.cfg.lock");
	pid_t ui = fork();
	if (ui == 0)
		_exit(write_ui_settings());
	pid_t owner = fork();
	if (owner == 0)
		_exit(write_owner_settings());
	int ui_status, owner_status;
	if (ui < 0 || owner < 0 || waitpid(ui, &ui_status, 0) < 0 || waitpid(owner, &owner_status, 0) < 0 ||
		!WIFEXITED(ui_status) || WEXITSTATUS(ui_status) || !WIFEXITED(owner_status) || WEXITSTATUS(owner_status))
		return 1;
	FILE* settings = fopen(file, "r");
	if (!settings)
		return 1;
	char contents[512] = {0};
	fread(contents, 1, sizeof(contents) - 1, settings);
	fclose(settings);
	int ok = strstr(contents, "screen_off_timeout=") && strstr(contents, "lyrics_enabled=") &&
		strstr(contents, "bass_filter_hz=") && strstr(contents, "soft_limiter=") &&
		strstr(contents, "sample_rate_follow=") && strstr(contents, "resampler_quality=") &&
		strstr(contents, "buffer_frames=");
	unlink(file);
	unlink("/tmp/nx_music_settings_test/settings.cfg.lock");
	rmdir(dir);
	if (!ok)
		return 1;
	puts("music settings concurrent writer test passed");
	return 0;
}
