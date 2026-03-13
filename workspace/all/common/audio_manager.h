#ifndef __AUDIO_MANAGER_H__
#define __AUDIO_MANAGER_H__

#include <stdbool.h>

// Audio sink types (mirrors msettings AUDIO_SINK_* values)
#define AUDIOMGR_SINK_DEFAULT 0
#define AUDIOMGR_SINK_BLUETOOTH 1
#define AUDIOMGR_SINK_USBDAC 2

// Initialize audio manager: detect sink, configure mixer, start device watcher, init HID
void AudioMgr_init(void);

// Shutdown audio manager: stop watcher, close HID, reset state
void AudioMgr_quit(void);

// Query current audio sink type
int AudioMgr_getSinkType(void);
bool AudioMgr_isBluetoothActive(void);
bool AudioMgr_isUSBDACActive(void);

// Get the preferred SDL audio device name for the current sink.
// Returns NULL — apps always use ALSA default, audiomon manages .asoundrc.
const char* AudioMgr_getPreferredDevice(void);

// Callback invoked on main thread when audio sink changes
typedef void (*AudioMgrCallback)(int sink_type);
void AudioMgr_setCallback(AudioMgrCallback cb);

// Poll for pending audio device changes (call from main thread).
// Re-detects sink, reconfigures mixer, invokes callback if changed.
// Returns true if a change was processed.
bool AudioMgr_pollEvents(void);

// HID media button events (from USB earphone/BT AVRCP buttons)
typedef enum {
	AUDIOMGR_HID_NONE = 0,
	AUDIOMGR_HID_VOLUME_UP,
	AUDIOMGR_HID_VOLUME_DOWN,
	AUDIOMGR_HID_NEXT_TRACK,
	AUDIOMGR_HID_PLAY_PAUSE,
	AUDIOMGR_HID_PREV_TRACK
} AudioMgrHIDEvent;

// Poll for HID media button events (call from main thread)
AudioMgrHIDEvent AudioMgr_pollHID(void);

#endif
