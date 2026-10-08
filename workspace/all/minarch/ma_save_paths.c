#include "ma_save_paths.h"
#include "config.h"
#include "defines.h"
#include <stdio.h>
#include <string.h>

void SavePaths_stripExtension(char* name) {
	char* tmp = strrchr(name, '.');
	if (tmp != NULL && strlen(tmp) > 2 && strlen(tmp) <= 5) {
		tmp[0] = '\0';
	}
}

int SavePaths_state(char* out, size_t size, const char* states_dir, const char* alt_name, int format, int slot) {
	char work_name[MAX_PATH];
	snprintf(work_name, sizeof(work_name), "%s", alt_name);
	int n;

	// This is only here for compatibility with older versions of minarch,
	// should probably be removed at some point in the future.
	if (format == STATE_FORMAT_SRM_EXTRADOT || format == STATE_FORMAT_SRM_UNCOMPRESSED_EXTRADOT) {
		SavePaths_stripExtension(work_name);

		if (slot == AUTO_RESUME_SLOT)
			n = snprintf(out, size, "%s/%s.state.auto", states_dir, work_name);
		else
			n = snprintf(out, size, "%s/%s.state.%i", states_dir, work_name, slot);
	} else if (format == STATE_FORMAT_SRM || format == STATE_FORMAT_SRM_UNCOMPRESSED) {
		SavePaths_stripExtension(work_name);

		if (slot == AUTO_RESUME_SLOT)
			n = snprintf(out, size, "%s/%s.state.auto", states_dir, work_name);
		else if (slot == 0)
			n = snprintf(out, size, "%s/%s.state", states_dir, work_name);
		else
			n = snprintf(out, size, "%s/%s.state%i", states_dir, work_name, slot);
	} else {
		n = snprintf(out, size, "%s/%s.st%i", states_dir, alt_name, slot);
	}
	return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

NetplaySavesMode SavePaths_netplayMode(const char* netplay_role, const char* netplay_saves_dir) {
	// The saves dir wins: flycast's client sets it without NETPLAY_ROLE.
	if (netplay_saves_dir && netplay_saves_dir[0])
		return NETPLAY_SAVES_COPY;
	if (netplay_role && netplay_role[0])
		return NETPLAY_SAVES_REAL;
	return NETPLAY_SAVES_NONE;
}

int SavePaths_autoResumeIsGame(const char* marker, const char* game_path, const char* sdcard_path) {
	size_t sd_len = strlen(sdcard_path);
	if (!marker || !marker[0] || strncmp(game_path, sdcard_path, sd_len) != 0)
		return 0;
	return strcmp(marker, game_path + sd_len) == 0;
}
