#include "audio_manager.h"
#include "api.h"
#include <msettings.h>
#include <stdio.h>
#include <stdlib.h>
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

// Internal state — single source of truth for audio sink
static bool bluetooth_active = false;
static bool usbdac_active = false;
static volatile bool pending_change = false;
static int last_known_sink = -1; // track msettings value for active polling
static int hid_fd = -1;
static AudioMgrCallback callback = NULL;
static bool initialized = false;

// ============ INTERNAL HELPERS ============

// Detect bluetooth by reading .asoundrc for bluealsa config
static bool detect_bluetooth_from_asoundrc(void) {
	const char* home = getenv("HOME");
	if (!home)
		return false;

	char path[512];
	snprintf(path, sizeof(path), "%s/.asoundrc", home);
	FILE* f = fopen(path, "r");
	if (!f)
		return false;

	char buf[256];
	bool found = false;
	while (fgets(buf, sizeof(buf), f)) {
		if (strstr(buf, "bluealsa")) {
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

// Check if a USB audio card is present by reading /proc/asound/cards
// This is non-blocking (pure file read), unlike amixer which can hang
// if the USB device was just removed.
static bool detect_usbdac_hardware(void) {
	FILE* f = fopen("/proc/asound/cards", "r");
	if (!f)
		return false;
	char line[128];
	bool found = false;
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "USB-Audio") || strstr(line, "USB Audio")) {
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

// Re-detect audio sink from msettings + hardware presence + .asoundrc
// Also syncs msettings if hardware state disagrees (e.g. audiomon hasn't caught up)
static void detect_sink(void) {
	int audio_sink = GetAudioSink();

	usbdac_active = (audio_sink == AUDIO_SINK_USBDAC);
	bluetooth_active = false;

	// Detect USB DAC by ALSA hardware presence (msettings may not be updated yet)
	bool hw_usb = detect_usbdac_hardware();
	if (!usbdac_active && hw_usb) {
		usbdac_active = true;
		// Sync msettings so SetRawVolume() uses the USB DAC path
		LOG_info("AudioMgr: syncing msettings to USBDAC (hw detected)\n");
		SetAudioSink(AUDIO_SINK_USBDAC);
	} else if (usbdac_active && !hw_usb) {
		usbdac_active = false;
		// USB DAC was removed, revert to default
		LOG_info("AudioMgr: syncing msettings to DEFAULT (hw removed)\n");
		SetAudioSink(AUDIO_SINK_DEFAULT);
	}

	// .asoundrc bluealsa check is more reliable than msettings for BT
	if (detect_bluetooth_from_asoundrc()) {
		bluetooth_active = true;
	} else if (audio_sink == AUDIO_SINK_BLUETOOTH) {
		bluetooth_active = true;
	}
}

// ============ MIXER CONFIGURATION ============

void AudioMgr_configureMixer(void) {
	if (bluetooth_active) {
		// Set all A2DP mixer controls to max for software volume control
		system("amixer scontrols 2>/dev/null | grep -i 'A2DP' | "
			   "sed \"s/.*'\\([^']*\\)'.*/\\1/\" | "
			   "while read ctrl; do amixer sset \"$ctrl\" 127 2>/dev/null; done");
	}

	if (usbdac_active) {
		// USB DACs appear as card 1, set common mixer controls to 100%
		system("amixer -c 1 sset PCM 100% 2>/dev/null; "
			   "amixer -c 1 sset Master 100% 2>/dev/null; "
			   "amixer -c 1 sset Speaker 100% 2>/dev/null; "
			   "amixer -c 1 sset Headphone 100% 2>/dev/null; "
			   "amixer -c 1 sset Headset 100% 2>/dev/null");
	}
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
	if (usbdac_active) {
		if (find_audio_hid_device(event_path, sizeof(event_path), false) == 0) {
			hid_fd = open(event_path, O_RDONLY | O_NONBLOCK);
			if (hid_fd >= 0)
				return;
		}
	}

	// Try Bluetooth AVRCP
	if (bluetooth_active) {
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

	detect_sink();
	last_known_sink = GetAudioSink();
	LOG_info("AudioMgr_init: bt=%d, usb=%d, sink=%d\n",
			 bluetooth_active, usbdac_active, last_known_sink);
	AudioMgr_configureMixer();

	// Set AUDIODEV env var
	if (bluetooth_active) {
		SDL_setenv("AUDIODEV", "bluealsa", 1);
	} else {
		SDL_setenv("AUDIODEV", "default", 1);
	}

	// Init HID if BT or USB active
	if (bluetooth_active || usbdac_active) {
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
	bluetooth_active = false;
	usbdac_active = false;
	pending_change = false;
	initialized = false;
}

int AudioMgr_getSinkType(void) {
	if (bluetooth_active)
		return AUDIOMGR_SINK_BLUETOOTH;
	if (usbdac_active)
		return AUDIOMGR_SINK_USBDAC;
	return AUDIOMGR_SINK_DEFAULT;
}

bool AudioMgr_isBluetoothActive(void) {
	return bluetooth_active;
}

bool AudioMgr_isUSBDACActive(void) {
	return usbdac_active;
}

const char* AudioMgr_getPreferredDevice(void) {
	if (usbdac_active) {
		return SND_findExternalAudioDevice();
	}
	if (bluetooth_active) {
		return NULL; // Let ALSA use default device (bluealsa via .asoundrc)
	}
	return SND_findSpeakerDevice();
}

void AudioMgr_setCallback(AudioMgrCallback cb) {
	callback = cb;
}

bool AudioMgr_pollEvents(void) {
	// Active polling fallback (~1s throttle): check if msettings sink changed
	// or USB DAC appeared. Inotify may not fire reliably for all changes.
	if (!pending_change) {
		static uint32_t last_poll_ms = 0;
		uint32_t now = SDL_GetTicks();
		if (now - last_poll_ms >= 1000) {
			last_poll_ms = now;
			int current_sink = GetAudioSink();
			if (current_sink != last_known_sink) {
				LOG_info("AudioMgr: sink changed via polling (%d -> %d)\n",
						 last_known_sink, current_sink);
				pending_change = true;
			}
			// Also check ALSA hardware presence for USB DAC
			if (!pending_change && !usbdac_active && detect_usbdac_hardware()) {
				LOG_info("AudioMgr: USB DAC detected via ALSA card 1\n");
				pending_change = true;
			}
			// Also check if USB DAC was removed
			if (!pending_change && usbdac_active && !detect_usbdac_hardware()) {
				LOG_info("AudioMgr: USB DAC removed (ALSA card 1 gone)\n");
				pending_change = true;
			}
		}
	}
	if (!pending_change)
		return false;
	pending_change = false;

	// Save old state
	bool was_bt = bluetooth_active;
	bool was_usb = usbdac_active;

	// Re-detect
	detect_sink();

	bool changed = (was_bt != bluetooth_active) || (was_usb != usbdac_active);

	LOG_info("AudioMgr: device change event (bt: %d->%d, usb: %d->%d, changed: %d)\n",
			 was_bt, bluetooth_active, was_usb, usbdac_active, changed);

	// Update tracked sink value
	last_known_sink = GetAudioSink();

	if (!changed)
		return false;

	// Reconfigure mixer for new sink
	AudioMgr_configureMixer();

	// Manage HID open/close
	if (bluetooth_active || usbdac_active) {
		hid_init();
	} else {
		hid_quit();
	}

	// Set AUDIODEV env var
	if (bluetooth_active) {
		SDL_setenv("AUDIODEV", "bluealsa", 1);
	} else {
		SDL_setenv("AUDIODEV", "default", 1);
	}

	// Invoke callback
	if (callback) {
		LOG_info("AudioMgr: invoking callback for sink type %d\n", AudioMgr_getSinkType());
		callback(AudioMgr_getSinkType());
	}

	return true;
}
