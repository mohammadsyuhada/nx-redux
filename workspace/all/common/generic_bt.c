/////////////////////////////////////////////////////////////////////////////////////////

// File: common/generic_bt.c
// Generic implementations of bluetooth functions, to be used by platforms that don't
// provide their own implementations.
// Used by: tg5050
// Library dependencies: pthread
// Tool dependencies: alsa, amixer, bluealsa, bluetoothctl
// Script dependencies: $SYSTEM_PATH//etc/bluetooth/bt_init.sh

// \note This files does not have an acompanying header, as all functions are declared in api.h
// with minimal fallback implementations
// \sa FALLBACK_IMPLEMENTATION

/////////////////////////////////////////////////////////////////////////////////////////

#include "defines.h"
#include "platform.h"
#include "api.h"
#include "utils.h"

#include <pthread.h>
#include <unistd.h>
#include <sys/select.h>

bool PLAT_hasBluetooth() {
	return true;
}
bool PLAT_bluetoothEnabled() {
	return CFG_getBluetooth();
}

#define btlog(fmt, ...) \
	LOG_note(PLAT_bluetoothDiagnosticsEnabled() ? LOG_INFO : LOG_DEBUG, fmt, ##__VA_ARGS__)

// Forward declaration
static int bt_run_cmd(const char* cmd, char* output, size_t output_len);

// Check if bluetoothd is running — prevents bluetoothctl from hanging
// when the daemon has crashed or wasn't restarted after sleep/wake.
static bool bt_daemon_alive(void) {
	return system("pgrep -x bluetoothd >/dev/null 2>&1") == 0;
}

// Run a system() call guarded by a bluetoothd liveness check.
// Returns -1 without executing if bluetoothd is not running.
static int bt_system(const char* cmd) {
	if (!bt_daemon_alive()) {
		LOG_warn("bluetoothd not running, skipping command: %s\n", cmd);
		return -1;
	}
	return system(cmd);
}

// Bluetoothctl version detection
static int bluetoothctl_major_version = 0;
static int bluetoothctl_minor_version = 0;

static void bt_detect_version(void) {
	static bool detected = false;
	if (detected)
		return;

	char output[256];
	if (bt_run_cmd("bluetoothctl --version 2>/dev/null | head -1", output, sizeof(output)) == 0) {
		// Parse version like "bluetoothctl: 5.54" or "5.78"
		int major = 0, minor = 0;
		if (sscanf(output, "bluetoothctl: %d.%d", &major, &minor) == 2 ||
			sscanf(output, "%d.%d", &major, &minor) == 2) {
			bluetoothctl_major_version = major;
			bluetoothctl_minor_version = minor;
			btlog("Detected bluetoothctl version %d.%d\n", major, minor);
		} else {
			// Default to 5.54 if detection fails
			bluetoothctl_major_version = 5;
			bluetoothctl_minor_version = 54;
			btlog("Failed to detect bluetoothctl version, assuming 5.54\n");
		}
	} else {
		// Assume older version if --version doesn't work
		bluetoothctl_major_version = 5;
		bluetoothctl_minor_version = 54;
		btlog("bluetoothctl --version failed, assuming 5.54\n");
	}
	detected = true;
}

// Helper to check if bluetoothctl version is >= specified version
static bool bt_version_gte(int major, int minor) {
	if (bluetoothctl_major_version > major)
		return true;
	if (bluetoothctl_major_version == major && bluetoothctl_minor_version >= minor)
		return true;
	return false;
}

// Device class definitions for parsing
#define COD_MAJOR_MASK 0x1F00
#define GET_MAJOR_CLASS(cod) ((cod & COD_MAJOR_MASK) >> 8)
#define BT_CLASS_AUDIO_VIDEO 0x04
#define BT_CLASS_PERIPHERAL 0x05

// Maximum discovered devices to track
#define MAX_DISCOVERED_DEVICES 64

typedef struct bt_dev_node {
	char addr[18];
	char name[249];
	BluetoothDeviceType kind;
	struct bt_dev_node* next;
} bt_dev_node_t;

static bt_dev_node_t* discovered_devices = NULL;
static pthread_mutex_t discovered_devices_mtx = PTHREAD_MUTEX_INITIALIZER;
static volatile bool bt_discovering = false;
static volatile bool bt_initialized = false;

// Helper to run a command and capture output.
// bluetoothctl commands get a 5s select() timeout as a safety net
// (e.g. if bluetoothd is briefly unavailable during sleep/wake).
static int bt_run_cmd(const char* cmd, char* output, size_t output_len) {
	// If command uses bluetoothctl, verify bluetoothd is running first
	// to avoid hanging indefinitely on D-Bus connection attempts
	if (strstr(cmd, "bluetoothctl") != NULL && !bt_daemon_alive()) {
		LOG_warn("bluetoothd not running, skipping command: %s\n", cmd);
		if (output && output_len > 0)
			output[0] = '\0';
		return -1;
	}

	btlog("Running command: %s\n", cmd);
	FILE* fp = popen(cmd, "r");
	if (!fp) {
		LOG_error("Failed to run command: %s\n", cmd);
		return -1;
	}

	if (output && output_len > 0) {
		output[0] = '\0';
		size_t total = 0;
		int use_timeout = (strstr(cmd, "bluetoothctl") != NULL);
		int fd = fileno(fp);
		char buf[256];

		while (total < output_len - 1) {
			if (use_timeout) {
				fd_set fds;
				struct timeval tv;
				FD_ZERO(&fds);
				FD_SET(fd, &fds);
				tv.tv_sec = 5;
				tv.tv_usec = 0;
				if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0)
					break; // timeout or error
			}
			if (!fgets(buf, sizeof(buf), fp))
				break;
			size_t len = strlen(buf);
			if (total + len < output_len) {
				strcpy(output + total, buf);
				total += len;
			}
		}
	}

	int status = pclose(fp);
	return WEXITSTATUS(status);
}

// Helper to add device to discovered list
static void bt_add_discovered_device(const char* addr, const char* name, BluetoothDeviceType kind) {
	pthread_mutex_lock(&discovered_devices_mtx);

	// Check if device already exists
	bt_dev_node_t* node = discovered_devices;
	while (node) {
		if (strcmp(node->addr, addr) == 0) {
			// Update name if it changed
			if (name && name[0] && strcmp(node->name, name) != 0) {
				strncpy(node->name, name, sizeof(node->name) - 1);
				node->name[sizeof(node->name) - 1] = '\0';
			}
			// Update kind if we have better info
			if (kind != BLUETOOTH_NONE) {
				node->kind = kind;
			}
			pthread_mutex_unlock(&discovered_devices_mtx);
			return;
		}
		node = node->next;
	}

	// Add new device
	bt_dev_node_t* new_node = (bt_dev_node_t*)malloc(sizeof(bt_dev_node_t));
	if (!new_node) {
		pthread_mutex_unlock(&discovered_devices_mtx);
		return;
	}

	strncpy(new_node->addr, addr, sizeof(new_node->addr) - 1);
	new_node->addr[sizeof(new_node->addr) - 1] = '\0';

	if (name && name[0]) {
		strncpy(new_node->name, name, sizeof(new_node->name) - 1);
		new_node->name[sizeof(new_node->name) - 1] = '\0';
	} else {
		strncpy(new_node->name, addr, sizeof(new_node->name) - 1);
		new_node->name[sizeof(new_node->name) - 1] = '\0';
	}

	new_node->kind = kind;
	new_node->next = discovered_devices;
	discovered_devices = new_node;

	btlog("Added discovered device: %s (%s) kind=%d\n", new_node->addr, new_node->name, kind);
	pthread_mutex_unlock(&discovered_devices_mtx);
}

// Helper to clear discovered devices list
static void bt_clear_discovered_devices(void) {
	pthread_mutex_lock(&discovered_devices_mtx);
	bt_dev_node_t* node = discovered_devices;
	while (node) {
		bt_dev_node_t* next = node->next;
		free(node);
		node = next;
	}
	discovered_devices = NULL;
	pthread_mutex_unlock(&discovered_devices_mtx);
}

// Helper to remove a device from discovered list (e.g., after pairing)
static void bt_remove_discovered_device(const char* addr) {
	pthread_mutex_lock(&discovered_devices_mtx);
	bt_dev_node_t** pp = &discovered_devices;
	while (*pp) {
		if (strcmp((*pp)->addr, addr) == 0) {
			bt_dev_node_t* to_free = *pp;
			*pp = (*pp)->next;
			free(to_free);
			pthread_mutex_unlock(&discovered_devices_mtx);
			return;
		}
		pp = &(*pp)->next;
	}
	pthread_mutex_unlock(&discovered_devices_mtx);
}

// Parse device class from bluetoothctl info output
static BluetoothDeviceType bt_parse_device_class(const char* info_output) {
	// Look for "Class:" line in bluetoothctl info output
	// Format: Class: 0x240404 (audio-card)
	const char* class_line = strstr(info_output, "Class:");
	if (class_line) {
		unsigned int class_val = 0;
		if (sscanf(class_line, "Class: 0x%x", &class_val) == 1) {
			int major = GET_MAJOR_CLASS(class_val);
			if (major == BT_CLASS_AUDIO_VIDEO) {
				return BLUETOOTH_AUDIO;
			} else if (major == BT_CLASS_PERIPHERAL) {
				return BLUETOOTH_CONTROLLER;
			}
		}
	}

	// Also check Icon field as fallback
	const char* icon_line = strstr(info_output, "Icon:");
	if (icon_line) {
		if (strstr(icon_line, "audio") || strstr(icon_line, "headset") || strstr(icon_line, "headphone")) {
			return BLUETOOTH_AUDIO;
		} else if (strstr(icon_line, "input-gaming") || strstr(icon_line, "input-keyboard") || strstr(icon_line, "input-mouse")) {
			return BLUETOOTH_CONTROLLER;
		}
	}

	return BLUETOOTH_NONE;
}

// Get device info using bluetoothctl
static BluetoothDeviceType bt_get_device_type(const char* addr) {
	char cmd[256];
	char output[2048];

	snprintf(cmd, sizeof(cmd), "bluetoothctl info %s 2>/dev/null", addr);
	if (bt_run_cmd(cmd, output, sizeof(output)) == 0) {
		return bt_parse_device_class(output);
	}
	return BLUETOOTH_NONE;
}

// Check if bluetooth adapter is powered on
static bool bt_is_powered(void) {
	char output[256];
	if (bt_run_cmd("bluetoothctl show 2>/dev/null | grep 'Powered:' | awk '{print $2}'", output, sizeof(output)) == 0) {
		return strstr(output, "yes") != NULL;
	}
	return false;
}

/////////////////////////////////

void PLAT_bluetoothInit() {
	LOG_info("BT init (generic Linux)\n");

	if (bt_initialized) {
		LOG_error("BT is already initialized.\n");
		return;
	}

	// Detect bluetoothctl version
	bt_detect_version();

	bt_initialized = true;
	// bluetoothd is always started at boot (launch.sh).
	// If BT is on, power on the adapter. If off, it's already powered off.
	if (CFG_getBluetooth()) {
		bt_system("bluetoothctl power on 2>/dev/null");
	}
}

void PLAT_bluetoothDeinit() {
	if (bt_initialized) {
		bt_clear_discovered_devices();
		bt_initialized = false;
	}
}

void PLAT_bluetoothEnable(bool shouldBeOn) {
	if (shouldBeOn) {
		btlog("Turning BT on...\n");
		// Start the BT stack if not already running, then power on
		system(SYSTEM_PATH "/etc/bluetooth/bt_init.sh start");
	} else {
		btlog("Turning BT off...\n");
		// Stop discovery if active
		if (bt_discovering) {
			bt_system("bluetoothctl scan off 2>/dev/null");
			system("pkill -f 'bluetoothctl scan on' 2>/dev/null");
			bt_discovering = false;
		}
		// Just power off the adapter — keep bluetoothd running so
		// bluetoothctl commands don't hang
		bt_system("bluetoothctl power off 2>/dev/null");
	}
	CFG_setBluetooth(shouldBeOn);
}

bool PLAT_bluetoothDiagnosticsEnabled() {
	return CFG_getBluetoothDiagnostics();
}

void PLAT_bluetoothDiagnosticsEnable(bool on) {
	CFG_setBluetoothDiagnostics(on);
}

void PLAT_bluetoothDiscovery(int on) {
	if (on) {
		btlog("Starting BT discovery.\n");
		// Clear old discovered devices
		bt_clear_discovered_devices();

		// Remove stale devices from BlueZ cache (devices that are neither
		// paired nor connected). This prevents old cached entries from
		// showing up as "Available" when they're no longer in range.
		{
			char rm_output[8192];
			if (bt_run_cmd("bluetoothctl devices 2>/dev/null", rm_output, sizeof(rm_output)) == 0) {
				// Get paired+connected addresses to preserve
				struct BT_devicePaired keep_list[32];
				int keep_count = PLAT_bluetoothPaired(keep_list, 32);

				char* line = strtok(rm_output, "\n");
				while (line) {
					char addr[18] = {0};
					if (strncmp(line, "Device ", 7) == 0 && sscanf(line, "Device %17s", addr) == 1) {
						int keep = 0;
						for (int i = 0; i < keep_count; i++) {
							if (strcmp(keep_list[i].remote_addr, addr) == 0) {
								keep = 1;
								break;
							}
						}
						if (!keep) {
							char cmd[256];
							snprintf(cmd, sizeof(cmd), "bluetoothctl remove %s 2>/dev/null", addr);
							bt_system(cmd);
						}
					}
					line = strtok(NULL, "\n");
				}
			}
		}

		// Start scanning in background. bluetoothctl must stay in the
		// foreground of its subshell to keep the D-Bus discovery session alive.
		bt_system("sh -c 'bluetoothctl --timeout 60 scan on >/dev/null 2>&1' &");
		bt_discovering = true;
	} else {
		btlog("Stopping BT discovery.\n");
		bt_system("bluetoothctl scan off 2>/dev/null");
		// Also try to kill any background scan processes
		system("pkill -f 'bluetoothctl scan on' 2>/dev/null");
		bt_discovering = false;
	}
}

bool PLAT_bluetoothDiscovering() {
	return bt_discovering;
}

int PLAT_bluetoothScan(struct BT_device* devices, int max) {
	if (!CFG_getBluetooth()) {
		return 0;
	}

	// Get list of discovered devices from bluetoothctl
	char output[8192];
	if (bt_run_cmd("bluetoothctl devices 2>/dev/null", output, sizeof(output)) != 0) {
		btlog("Failed to get device list\n");
		return 0;
	}

	// Parse output: "Device XX:XX:XX:XX:XX:XX DeviceName"
	char* line = strtok(output, "\n");
	while (line) {
		char addr[18] = {0};
		char name[249] = {0};

		// Parse "Device XX:XX:XX:XX:XX:XX Name"
		if (strncmp(line, "Device ", 7) == 0) {
			if (sscanf(line, "Device %17s", addr) == 1) {
				// Get name (everything after the address)
				char* name_start = line + 7 + 18; // "Device " + "XX:XX:XX:XX:XX:XX "
				if (*name_start) {
					strncpy(name, name_start, sizeof(name) - 1);
				}

				// Get device type — only show audio and controller devices
				BluetoothDeviceType kind = bt_get_device_type(addr);
				if (kind == BLUETOOTH_AUDIO || kind == BLUETOOTH_CONTROLLER) {
					bt_add_discovered_device(addr, name, kind);
				}
			}
		}
		line = strtok(NULL, "\n");
	}

	// Build a set of paired addresses/names to skip (callers also deduplicate,
	// but filtering here avoids returning paired devices as "available")
	struct BT_devicePaired paired_list[32];
	int paired_count = PLAT_bluetoothPaired(paired_list, 32);

	// Copy discovered devices to output array
	int count = 0;
	pthread_mutex_lock(&discovered_devices_mtx);
	bt_dev_node_t* node = discovered_devices;
	while (node && count < max) {
		// Skip paired devices (by address or by name, since some devices
		// like 8BitDo use different MACs for classic BT vs BLE)
		int is_paired = 0;
		for (int i = 0; i < paired_count; i++) {
			if (strcmp(paired_list[i].remote_addr, node->addr) == 0) {
				is_paired = 1;
				break;
			}
			if (node->name[0] && paired_list[i].remote_name[0] &&
				strcmp(paired_list[i].remote_name, node->name) == 0) {
				is_paired = 1;
				break;
			}
		}
		if (is_paired) {
			node = node->next;
			continue;
		}

		struct BT_device* device = &devices[count];
		strncpy(device->addr, node->addr, sizeof(device->addr) - 1);
		device->addr[sizeof(device->addr) - 1] = '\0';
		strncpy(device->name, node->name, sizeof(device->name) - 1);
		device->name[sizeof(device->name) - 1] = '\0';
		device->kind = node->kind;

		btlog("Scan result: %s (%s) kind=%d\n", device->addr, device->name, device->kind);
		count++;
		node = node->next;
	}
	pthread_mutex_unlock(&discovered_devices_mtx);

	return count;
}

int PLAT_bluetoothPaired(struct BT_devicePaired* paired, int max) {
	if (!CFG_getBluetooth()) {
		return 0;
	}

	// Get list of paired and connected devices. Some controllers (e.g. 8BitDo)
	// use different addresses for pairing vs connection, so a device can be
	// connected without being in the "Paired" list. Include both.
	char output[8192];
	char connected_output[4096];
	int ret;

	if (bt_version_gte(5, 70)) {
		ret = bt_run_cmd("bluetoothctl devices Paired 2>/dev/null", output, sizeof(output));
	} else {
		ret = bt_run_cmd("bluetoothctl paired-devices 2>/dev/null", output, sizeof(output));
	}

	if (ret != 0) {
		output[0] = '\0';
	}

	// Also include trusted devices. Our pair flow uses trust + pair, but some
	// devices (e.g. Galaxy Buds, 8BitDo LE) don't complete bonding properly,
	// so they show as Trusted but not Paired. From the user's perspective
	// these are "paired" devices they explicitly added.
	if (bt_version_gte(5, 70)) {
		char trusted_output[4096];
		if (bt_run_cmd("bluetoothctl devices Trusted 2>/dev/null", trusted_output, sizeof(trusted_output)) == 0 &&
			trusted_output[0]) {
			size_t len = strlen(output);
			if (len > 0 && output[len - 1] != '\n') {
				strncat(output, "\n", sizeof(output) - len - 1);
				len++;
			}
			strncat(output, trusted_output, sizeof(output) - len - 1);
		}
	}

	// Also get connected devices and append any that aren't already listed
	if (bt_run_cmd("bluetoothctl devices Connected 2>/dev/null", connected_output, sizeof(connected_output)) == 0 &&
		connected_output[0]) {
		size_t len = strlen(output);
		if (len > 0 && output[len - 1] != '\n') {
			strncat(output, "\n", sizeof(output) - len - 1);
			len++;
		}
		strncat(output, connected_output, sizeof(output) - len - 1);
	}

	int count = 0;
	char* line = strtok(output, "\n");
	while (line && count < max) {
		char addr[18] = {0};
		char name[249] = {0};

		// Parse "Device XX:XX:XX:XX:XX:XX Name"
		if (strncmp(line, "Device ", 7) == 0) {
			if (sscanf(line, "Device %17s", addr) == 1) {
				// Get name (everything after the address)
				char* name_start = line + 7 + 18;
				if (*name_start) {
					strncpy(name, name_start, sizeof(name) - 1);
				}

				// Skip if already in the list (device may be both paired and connected)
				int duplicate = 0;
				for (int i = 0; i < count; i++) {
					if (strcmp(paired[i].remote_addr, addr) == 0) {
						duplicate = 1;
						break;
					}
				}
				if (duplicate) {
					line = strtok(NULL, "\n");
					continue;
				}

				struct BT_devicePaired* device = &paired[count];
				strncpy(device->remote_addr, addr, sizeof(device->remote_addr) - 1);
				device->remote_addr[sizeof(device->remote_addr) - 1] = '\0';
				strncpy(device->remote_name, name, sizeof(device->remote_name) - 1);
				device->remote_name[sizeof(device->remote_name) - 1] = '\0';
				device->is_bonded = true;
				device->rssi = -50; // Default value, actual RSSI requires active connection

				// Check if connected
				char cmd[256];
				char info_output[1024];
				snprintf(cmd, sizeof(cmd), "bluetoothctl info %s 2>/dev/null | grep 'Connected: yes'", addr);
				device->is_connected = (bt_run_cmd(cmd, info_output, sizeof(info_output)) == 0 &&
										strstr(info_output, "Connected: yes") != NULL);

				btlog("Paired device: %s (%s) connected=%d\n", device->remote_addr, device->remote_name, device->is_connected);
				count++;
			}
		}
		line = strtok(NULL, "\n");
	}

	return count;
}

void PLAT_bluetoothPair(char* addr) {
	btlog("Pairing with %s\n", addr);

	char cmd[256];

	// Trust first for automatic reconnection
	snprintf(cmd, sizeof(cmd), "bluetoothctl trust %s 2>/dev/null", addr);
	bt_system(cmd);

	// Pair with built-in NoInputNoOutput agent (auto-accepts confirmation)
	snprintf(cmd, sizeof(cmd), "bluetoothctl --agent NoInputNoOutput pair %s 2>/dev/null", addr);
	int ret = bt_system(cmd);
	if (ret != 0) {
		LOG_error("BT pair failed: %d\n", ret);
	}

	// Connect after pairing
	snprintf(cmd, sizeof(cmd), "bluetoothctl connect %s 2>/dev/null", addr);
	bt_system(cmd);

	// Remove from discovered list since it's now paired
	bt_remove_discovered_device(addr);
}

void PLAT_bluetoothUnpair(char* addr) {
	btlog("Unpairing %s\n", addr);

	char cmd[256];

	// Disconnect first if connected
	snprintf(cmd, sizeof(cmd), "bluetoothctl disconnect %s 2>/dev/null", addr);
	bt_system(cmd);

	// Remove the device (this unpairs and removes from BlueZ cache)
	snprintf(cmd, sizeof(cmd), "bluetoothctl remove %s 2>/dev/null", addr);
	int ret = bt_system(cmd);
	if (ret != 0) {
		LOG_error("BT unpair failed\n");
	}

	// Also remove from our internal discovered list
	bt_remove_discovered_device(addr);
}

void PLAT_bluetoothConnect(char* addr) {
	btlog("Connecting to %s\n", addr);

	char cmd[256];
	snprintf(cmd, sizeof(cmd), "bluetoothctl connect %s 2>/dev/null", addr);
	int ret = bt_system(cmd);
	if (ret != 0) {
		LOG_error("BT connect failed: %d\n", ret);
	}
	LOG_info("BT connect returned: %d\n", ret);
}

void PLAT_bluetoothDisconnect(char* addr) {
	btlog("Disconnecting from %s\n", addr);

	char cmd[256];
	snprintf(cmd, sizeof(cmd), "bluetoothctl disconnect %s 2>/dev/null", addr);
	int ret = bt_system(cmd);
	if (ret != 0) {
		LOG_error("BT disconnect failed: %d\n", ret);
	}
}

bool PLAT_bluetoothConnected() {
	// Use bluetoothctl to detect both classic BT and BLE connections
	char output[2048];
	if (bt_run_cmd("bluetoothctl devices Connected 2>/dev/null", output, sizeof(output)) == 0) {
		// If output contains any "Device " line, something is connected
		return strstr(output, "Device ") != NULL;
	}
	return false;
}

int PLAT_bluetoothVolume() {
	// Try to get volume from ALSA mixer for bluealsa
	char output[256];
	int vol = 100; // Default to 100%

	// Try bluealsa-aplay volume or amixer
	if (bt_run_cmd("amixer -D bluealsa get 'A2DP' 2>/dev/null | grep -o '[0-9]*%' | head -1 | tr -d '%'", output, sizeof(output)) == 0) {
		int parsed_vol;
		if (sscanf(output, "%d", &parsed_vol) == 1) {
			vol = parsed_vol;
		}
	}

	btlog("BT volume: %d\n", vol);
	return vol;
}

void PLAT_bluetoothSetVolume(int vol) {
	if (vol > 100)
		vol = 100;
	if (vol < 0)
		vol = 0;

	char cmd[256];
	// Try to set bluealsa volume
	snprintf(cmd, sizeof(cmd), "amixer -D bluealsa set 'A2DP' %d%% 2>/dev/null", vol);
	system(cmd);

	btlog("Set BT volume: %d\n", vol);
}

// bt_device_watcher.c

#include <sys/inotify.h>

#define WATCHED_DIR_FMT "%s"
#define WATCHED_FILE ".asoundrc"
#define EVENT_BUF_LEN (1024 * (sizeof(struct inotify_event) + NAME_MAX + 1))

static pthread_t watcher_thread;
static int inotify_fd = -1;
static int dir_watch_fd = -1;
static int file_watch_fd = -1;
static volatile int running = 0;
static void (*callback_fn)(int device, int watch_event) = NULL;
static char watched_dir[MAX_PATH];
static char watched_file_path[MAX_PATH];

// Function to detect audio device type from .asoundrc content
static int detect_audio_device_type() {
	FILE* file = fopen(watched_file_path, "r");
	if (!file) {
		//LOG_info("detect_audio_device_type: .asoundrc not found, defaulting to AUDIO_SINK_DEFAULT\n");
		return AUDIO_SINK_DEFAULT;
	}

	char line[256];
	int is_bluetooth = 0;
	int is_usb_dac = 0;

	while (fgets(line, sizeof(line), file)) {
		if (strstr(line, "type bluealsa") || strstr(line, "defaults.bluealsa.device")) {
			//LOG_info("detect_audio_device_type: found bluealsa\n");
			is_bluetooth = 1;
			break;
		}
		if (strstr(line, "type hw")) {
			//LOG_info("detect_audio_device_type: found hw card\n");
			is_usb_dac = 1;
			break;
		}
	}

	fclose(file);

	if (is_bluetooth) {
		return AUDIO_SINK_BLUETOOTH;
	} else if (is_usb_dac) {
		return AUDIO_SINK_USBDAC;
	} else {
		return AUDIO_SINK_DEFAULT;
	}
}

static void add_file_watch() {
	if (file_watch_fd >= 0)
		return; // already watching

	file_watch_fd = inotify_add_watch(inotify_fd, watched_file_path,
									  IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF);
	if (file_watch_fd < 0) {
		if (errno != ENOENT) // ENOENT means file doesn't exist yet - no error needed
			LOG_error("PLAT_audioDeviceWatchRegister: failed to add file watch: %s\n", strerror(errno));
	} else {
		LOG_info("Watching file: %s\n", watched_file_path);
	}
}

static void remove_file_watch() {
	if (file_watch_fd >= 0) {
		inotify_rm_watch(inotify_fd, file_watch_fd);
		file_watch_fd = -1;
		LOG_info("Stopped watching file: %s\n", watched_file_path);
	}
}

static void* watcher_thread_func(void* arg) {
	char buffer[EVENT_BUF_LEN];

	// At start try to watch file if exists
	add_file_watch();

	while (running) {
		int length = read(inotify_fd, buffer, EVENT_BUF_LEN);
		if (length < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				sleep(1);
				continue;
			}
			LOG_error("inotify read error: %s\n", strerror(errno));
			break;
		}

		for (int i = 0; i < length;) {
			struct inotify_event* event = (struct inotify_event*)&buffer[i];

			if (event->wd == dir_watch_fd) {
				if (event->len > 0 && strcmp(event->name, WATCHED_FILE) == 0) {
					if (event->mask & IN_CREATE) {
						add_file_watch();
						int device_type = detect_audio_device_type();
						if (callback_fn)
							callback_fn(device_type, DIRWATCH_CREATE);
					}
					// No need to react to this, we handle it via file watch
					//else if (event->mask & IN_DELETE) {
					//    remove_file_watch();
					//    if (callback_fn) callback_fn(AUDIO_SINK_DEFAULT, DIRWATCH_DELETE);
					//}
				}
			} else if (event->wd == file_watch_fd) {
				if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF)) {
					if (event->mask & IN_DELETE_SELF) {
						remove_file_watch();
						if (callback_fn)
							callback_fn(AUDIO_SINK_DEFAULT, FILEWATCH_DELETE);
					}
					// No need to react to this, it usually comes paired with FILEWATCH_MODIFY
					//else if (event->mask & IN_CLOSE_WRITE) {
					//	if (callback_fn) callback_fn(AUDIO_SINK_BLUETOOTH, FILEWATCH_CLOSE_WRITE);
					//}
					else if (event->mask & IN_MODIFY) {
						int device_type = detect_audio_device_type();
						if (callback_fn)
							callback_fn(device_type, FILEWATCH_MODIFY);
					}
				}
			}

			i += sizeof(struct inotify_event) + event->len;
		}
	}

	return NULL;
}

void PLAT_audioDeviceWatchRegister(void (*cb)(int device, int event)) {
	if (running)
		return; // Already running

	callback_fn = cb;

	const char* home = getenv("HOME");
	if (!home) {
		LOG_error("PLAT_audioDeviceWatchRegister: HOME environment variable not set\n");
		return;
	}

	snprintf(watched_dir, MAX_PATH, WATCHED_DIR_FMT, home);
	snprintf(watched_file_path, MAX_PATH, "%s/%s", watched_dir, WATCHED_FILE);

	LOG_info("PLAT_audioDeviceWatchRegister: Watching directory %s\n", watched_dir);
	LOG_info("PLAT_audioDeviceWatchRegister: Watching file %s\n", watched_file_path);

	inotify_fd = inotify_init1(IN_NONBLOCK);
	if (inotify_fd < 0) {
		LOG_error("PLAT_audioDeviceWatchRegister: failed to initialize inotify\n");
		return;
	}

	dir_watch_fd = inotify_add_watch(inotify_fd, watched_dir, IN_CREATE | IN_DELETE);
	if (dir_watch_fd < 0) {
		LOG_error("PLAT_audioDeviceWatchRegister: failed to add directory watch\n");
		close(inotify_fd);
		inotify_fd = -1;
		return;
	}

	file_watch_fd = -1;

	running = 1;
	if (pthread_create(&watcher_thread, NULL, watcher_thread_func, NULL) != 0) {
		LOG_error("PLAT_audioDeviceWatchRegister: failed to create thread\n");
		inotify_rm_watch(inotify_fd, dir_watch_fd);
		close(inotify_fd);
		inotify_fd = -1;
		dir_watch_fd = -1;
		running = 0;
	}
}

void PLAT_audioDeviceWatchUnregister(void) {
	if (!running)
		return;

	running = 0;
	pthread_join(watcher_thread, NULL);

	if (file_watch_fd >= 0)
		inotify_rm_watch(inotify_fd, file_watch_fd);
	if (dir_watch_fd >= 0)
		inotify_rm_watch(inotify_fd, dir_watch_fd);
	if (inotify_fd >= 0)
		close(inotify_fd);

	inotify_fd = -1;
	dir_watch_fd = -1;
	file_watch_fd = -1;
	callback_fn = NULL;
}
