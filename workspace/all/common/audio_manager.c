#include "audio_manager.h"
#include "api.h"
#include <msettings.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

// Linux input event definitions (avoid including linux/input.h due to conflicts)
#define EV_KEY 0x01
#define KEY_VOLUMEDOWN 114
#define KEY_VOLUMEUP 115
#define KEY_NEXTSONG 163
#define KEY_PLAYPAUSE 164
#define KEY_PREVIOUSSONG 165
#define KEY_PLAYCD 200
#define KEY_PAUSECD 201

// Input event struct for 64-bit systems (24 bytes)
struct input_event_raw {
	uint64_t tv_sec;
	uint64_t tv_usec;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

// Internal state — reads msettings AudioSink (written by audiomon)
static int current_sink = AUDIO_SINK_DEFAULT;
static volatile bool pending_change = false;
static int last_known_sink = -1;
static int hid_fd = -1;
static AudioMgrCallback callback = NULL;
static bool initialized = false;

// ============ INTERNAL HELPERS ============

// Read sink type from msettings (audiomon is the writer)
static void read_sink(void) {
	int audio_sink = GetAudioSink();
	current_sink = audio_sink;
}

// ============ HID MEDIA BUTTONS ============

// Find USB audio HID device by scanning /proc/bus/input/devices
static int find_audio_hid_device(char* event_path, size_t path_size, bool find_bluetooth) {
	FILE* f = fopen("/proc/bus/input/devices", "r");
	if (!f)
		return -1;

	char line[512];
	char name[256] = {0};
	char handlers[256] = {0};
	bool is_usb = false;
	bool is_bluetooth_avrcp = false;
	bool has_kbd = false;

	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "N: Name=", 8) == 0) {
			strncpy(name, line + 8, sizeof(name) - 1);
			handlers[0] = 0;
			is_usb = false;
			is_bluetooth_avrcp = false;
			has_kbd = false;
			if (strstr(name, "AVRCP")) {
				is_bluetooth_avrcp = true;
			}
		} else if (strncmp(line, "P: Phys=", 8) == 0) {
			if (strstr(line, "usb-")) {
				is_usb = true;
			}
		} else if (strncmp(line, "H: Handlers=", 12) == 0) {
			strncpy(handlers, line + 12, sizeof(handlers) - 1);
			if (strstr(handlers, "kbd")) {
				has_kbd = true;
			}
		} else if (line[0] == '\n') {
			bool match = false;
			if (find_bluetooth && is_bluetooth_avrcp && has_kbd) {
				match = true;
			} else if (!find_bluetooth && is_usb && has_kbd) {
				match = true;
			}

			if (match && handlers[0]) {
				char* event_ptr = strstr(handlers, "event");
				if (event_ptr) {
					int event_num = -1;
					sscanf(event_ptr, "event%d", &event_num);
					if (event_num >= 0) {
						snprintf(event_path, path_size, "/dev/input/event%d", event_num);
						fclose(f);
						return 0;
					}
				}
			}
		}
	}

	fclose(f);
	return -1;
}

static void hid_init(void) {
	if (hid_fd >= 0) {
		close(hid_fd);
		hid_fd = -1;
	}

	char event_path[64];

	// Try USB DAC HID first
	if (current_sink == AUDIO_SINK_USBDAC) {
		if (find_audio_hid_device(event_path, sizeof(event_path), false) == 0) {
			hid_fd = open(event_path, O_RDONLY | O_NONBLOCK);
			if (hid_fd >= 0)
				return;
		}
	}

	// Try Bluetooth AVRCP
	if (current_sink == AUDIO_SINK_BLUETOOTH) {
		if (find_audio_hid_device(event_path, sizeof(event_path), true) == 0) {
			hid_fd = open(event_path, O_RDONLY | O_NONBLOCK);
			if (hid_fd >= 0)
				return;
		}
	}
}

static void hid_quit(void) {
	if (hid_fd >= 0) {
		close(hid_fd);
		hid_fd = -1;
	}
}

AudioMgrHIDEvent AudioMgr_pollHID(void) {
	if (hid_fd < 0)
		return AUDIOMGR_HID_NONE;

	struct input_event_raw ev;
	while (read(hid_fd, &ev, sizeof(ev)) == sizeof(ev)) {
		if (ev.type == EV_KEY && ev.value == 1) {
			switch (ev.code) {
			case KEY_VOLUMEUP:
				return AUDIOMGR_HID_VOLUME_UP;
			case KEY_VOLUMEDOWN:
				return AUDIOMGR_HID_VOLUME_DOWN;
			case KEY_NEXTSONG:
				return AUDIOMGR_HID_NEXT_TRACK;
			case KEY_PLAYPAUSE:
			case KEY_PLAYCD:
			case KEY_PAUSECD:
				return AUDIOMGR_HID_PLAY_PAUSE;
			case KEY_PREVIOUSSONG:
				return AUDIOMGR_HID_PREV_TRACK;
			}
		}
	}

	return AUDIOMGR_HID_NONE;
}

// ============ DEVICE WATCHER CALLBACK ============

// Called from inotify watcher thread — must NOT do SDL operations here.
static void watcher_callback(int device_type, int event) {
	(void)device_type;
	(void)event;
	pending_change = true;
}

// ============ PUBLIC API ============

void AudioMgr_init(void) {
	if (initialized)
		return;

	read_sink();
	last_known_sink = current_sink;

	// Init HID if BT or USB active
	if (current_sink != AUDIO_SINK_DEFAULT) {
		hid_init();
	}

	// Register inotify watcher for device changes
	PLAT_audioDeviceWatchRegister(watcher_callback);

	initialized = true;
}

void AudioMgr_quit(void) {
	if (!initialized)
		return;

	PLAT_audioDeviceWatchUnregister();
	hid_quit();
	callback = NULL;
	current_sink = AUDIO_SINK_DEFAULT;
	pending_change = false;
	initialized = false;
}

int AudioMgr_getSinkType(void) {
	if (current_sink == AUDIO_SINK_BLUETOOTH)
		return AUDIOMGR_SINK_BLUETOOTH;
	if (current_sink == AUDIO_SINK_USBDAC)
		return AUDIOMGR_SINK_USBDAC;
	return AUDIOMGR_SINK_DEFAULT;
}

bool AudioMgr_isBluetoothActive(void) {
	return current_sink == AUDIO_SINK_BLUETOOTH;
}

bool AudioMgr_isUSBDACActive(void) {
	return current_sink == AUDIO_SINK_USBDAC;
}

const char* AudioMgr_getPreferredDevice(void) {
	return NULL; // Always use ALSA default — audiomon manages .asoundrc
}

void AudioMgr_setCallback(AudioMgrCallback cb) {
	callback = cb;
}

bool AudioMgr_pollEvents(void) {
	// Poll msettings AudioSink (written by audiomon) for changes
	if (!pending_change) {
		static uint32_t last_poll_ms = 0;
		uint32_t now = SDL_GetTicks();
		if (now - last_poll_ms >= 1000) {
			last_poll_ms = now;
			int new_sink = GetAudioSink();
			if (new_sink != last_known_sink) {
				pending_change = true;
			}
		}
	}
	if (!pending_change)
		return false;
	pending_change = false;

	// Save old state
	int old_sink = current_sink;

	// Re-read from msettings
	read_sink();
	last_known_sink = current_sink;

	if (current_sink == old_sink)
		return false;

	// Manage HID open/close
	if (current_sink != AUDIO_SINK_DEFAULT) {
		hid_init();
	} else {
		hid_quit();
	}

	// Invoke callback
	if (callback) {
		callback(AudioMgr_getSinkType());
	}

	return true;
}
