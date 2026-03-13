#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "msettings.h"

// Detect USB DAC by reading /proc/asound/cards (non-blocking)
static int detect_usbdac(void) {
	FILE* f = fopen("/proc/asound/cards", "r");
	if (!f)
		return 0;
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "USB-Audio") || strstr(line, "USB Audio")) {
			fclose(f);
			return 1;
		}
	}
	fclose(f);
	return 0;
}

// Detect Bluetooth by reading .asoundrc for bluealsa config
static int detect_bluetooth(void) {
	// Use USERDATA_PATH (where audiomon writes .asoundrc) instead of HOME,
	// because standalone emulators may override HOME to their own config dir.
	const char* userdata = getenv("USERDATA_PATH");
	if (!userdata)
		userdata = getenv("HOME");
	if (!userdata)
		return 0;
	char path[512];
	snprintf(path, sizeof(path), "%s/.asoundrc", userdata);
	FILE* f = fopen(path, "r");
	if (!f)
		return 0;
	char buf[256];
	while (fgets(buf, sizeof(buf), f)) {
		if (strstr(buf, "bluealsa")) {
			fclose(f);
			return 1;
		}
	}
	fclose(f);
	return 0;
}

int main(int argc, char* argv[]) {
	InitSettings();

	// Sync audio sink so SetRawVolume() uses the correct mixer path
	if (detect_bluetooth())
		SetAudioSink(AUDIO_SINK_BLUETOOTH);
	else if (detect_usbdac())
		SetAudioSink(AUDIO_SINK_USBDAC);
	else
		SetAudioSink(AUDIO_SINK_DEFAULT);

	sleep(1);
	SetVolume(GetVolume());
	SetBrightness(GetBrightness());
	return 0;
}
