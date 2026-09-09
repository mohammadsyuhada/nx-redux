#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "msettings.h"
#include "msettings_shm.h"

#define SHM_KEY MSETTINGS_SHM_KEY

static SettingsShm* shm_open_rw(void) {
	int fd = shm_open(SHM_KEY, O_RDWR, 0644);
	if (fd == -1)
		return NULL;
	SettingsShm* s = mmap(NULL, sizeof(SettingsShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (s == MAP_FAILED)
		return NULL;
	return s;
}

static int get_volume(SettingsShm* s) {
	if (s->mute && s->toggled_volume != SETTINGS_DEFAULT_MUTE_NO_CHANGE)
		return s->toggled_volume;
	if (s->jack || s->audiosink != 0)
		return s->headphones;
	return s->speaker;
}

static void save_settings(SettingsShm* s) {
	const char* userdata = getenv("USERDATA_PATH");
	if (!userdata)
		userdata = "/mnt/SDCARD/.userdata/" PLATFORM; // was hardcoded tg5050
	char path[256];
	snprintf(path, sizeof(path), "%s/msettings.bin", userdata);
	int fd = open(path, O_CREAT | O_WRONLY, 0644);
	if (fd >= 0) {
		write(fd, s, sizeof(SettingsShm));
		close(fd);
	}
}

static void usage(void) {
	fprintf(stderr, "Usage: osdctl get <property>\n"
					"       osdctl set <property> <value>\n"
					"\n"
					"Properties:\n"
					"  volume      (0-20)\n"
					"  gamevolume  (0-20)\n"
					"  musicvolume (0-20)\n"
					"  brightness  (0-10)\n"
					"  fanspeed    (-3 to 100)\n"
					"  mute        (0-1)\n"
					"  rumble      (0-1) master motor switch\n"
					"  rumblestrength (0 normal, 1 light, 2 strong)\n");
	exit(1);
}

int main(int argc, char* argv[]) {
	if (argc < 3)
		usage();

	const char* cmd = argv[1];
	const char* prop = argv[2];

	SettingsShm* s = shm_open_rw();
	if (!s) {
		fprintf(stderr, "osdctl: shared memory not available\n");
		return 1;
	}

	// SetRaw* uses libmsettings' process-global state, so initialize it for
	// every setter before applying either hardware or persisted settings.
	int settings_initialized = 0;
	if (strcmp(cmd, "set") == 0) {
		if (!getenv("USERDATA_PATH"))
			setenv("USERDATA_PATH", "/mnt/SDCARD/.userdata/" PLATFORM, 0);
		InitSettings();
		settings_initialized = 1;
	}

	if (strcmp(cmd, "get") == 0) {
		if (strcmp(prop, "volume") == 0)
			printf("%d\n", get_volume(s));
		else if (strcmp(prop, "gamevolume") == 0) {
			int value = s->game_volume > 0 ? s->game_volume - 1 : 20;
			if (value < 0)
				value = 0;
			if (value > 20)
				value = 20;
			printf("%d\n", value);
		} else if (strcmp(prop, "musicvolume") == 0) {
			int value = s->music_volume > 0 ? s->music_volume - 1 : 20;
			if (value < 0)
				value = 0;
			if (value > 20)
				value = 20;
			printf("%d\n", value);
		} else if (strcmp(prop, "brightness") == 0)
			printf("%d\n", s->brightness);
#ifdef HAS_FAN
		else if (strcmp(prop, "fanspeed") == 0)
			printf("%d\n", s->fanSpeed);
#endif
		else if (strcmp(prop, "mute") == 0)
			printf("%d\n", s->mute);
		else if (strcmp(prop, "rumble") == 0)
			printf("%d\n", !s->rumble_off);
		else if (strcmp(prop, "rumblestrength") == 0)
			printf("%d\n", s->rumble_strength);
		else
			usage();
	} else if (strcmp(cmd, "set") == 0) {
		if (argc < 4)
			usage();
		int value = atoi(argv[3]);
		if (strcmp(prop, "volume") == 0 || strcmp(prop, "mute") == 0) {
			if (!getenv("USERDATA_PATH"))
				setenv("USERDATA_PATH", "/mnt/SDCARD/.userdata/" PLATFORM, 0);
			InitSettings();
			settings_initialized = 1;
		}


		if (strcmp(prop, "volume") == 0) {
			// Update shm
			if (s->mute)
				goto done; // don't change volume while muted
			if (s->jack || s->audiosink != 0)
				s->headphones = value;
			else
				s->speaker = value;
			// Apply to hardware
			SetRawVolume(value * 5); // scaleVolume: 0-20 → 0-100
			save_settings(s);
		} else if (strcmp(prop, "gamevolume") == 0 || strcmp(prop, "musicvolume") == 0) {
			if (value < 0)
				value = 0;
			if (value > 20)
				value = 20;
			if (strcmp(prop, "gamevolume") == 0) {
				s->game_volume = value + 1;
				save_settings(s);
				// audiomon owns the route and applies the softvol control. The
				// signal wakes its bounded settings poll immediately.
				system("killall -USR1 audiomon.elf 2>/dev/null");
			} else {
				s->music_volume = value + 1;
				save_settings(s);
			}
		} else if (strcmp(prop, "brightness") == 0) {
			s->brightness = value;
			int raw = (value <= 0) ? 10 : (value >= 10) ? 220
														: 10 + 21 * value;
			SetRawBrightness(raw);
			save_settings(s);
#ifdef HAS_FAN
		} else if (strcmp(prop, "fanspeed") == 0) {
			s->fanSpeed = value;
			SetRawFanSpeed(value);
			save_settings(s);
#endif
		} else if (strcmp(prop, "mute") == 0) {
			s->mute = value;
			if (value) {
				if (s->toggled_volume != SETTINGS_DEFAULT_MUTE_NO_CHANGE)
					SetRawVolume(s->toggled_volume * 5);
				else
					SetRawVolume(0);
			} else {
				int vol = (s->jack || s->audiosink != 0) ? s->headphones : s->speaker;
				SetRawVolume(vol * 5);
			}
			save_settings(s);
		} else if (strcmp(prop, "rumble") == 0) {
			// Master motor switch: the VIB thread in every app reads it live
			// from shm, so a game that is rumbling stops within a tick.
			s->rumble_off = !value;
			save_settings(s);
		} else if (strcmp(prop, "rumblestrength") == 0) {
			s->rumble_strength = (value < 0 || value > 2) ? 0 : value;
			save_settings(s);
		} else {
			usage();
		}
	} else {
		usage();
	}

done:
	if (settings_initialized)
		QuitSettings();
	munmap(s, sizeof(SettingsShm));
	return 0;
}
