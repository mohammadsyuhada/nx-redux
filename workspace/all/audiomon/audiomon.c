// audiomon.c
// Monitors Bluetooth device connections and USB-C DAC connections, updating .asoundrc for audio sinks

#include <dbus/dbus.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <syslog.h>
#include <errno.h>
#include <stdbool.h>

#include "msettings.h"
#include "defines.h"

#define AUDIO_FILE USERDATA_PATH "/.asoundrc"
#define UUID_A2DP "0000110b-0000-1000-8000-00805f9b34fb"

enum DeviceType {
	DEVICE_BLUETOOTH,
	DEVICE_USB_AUDIO
};

static bool use_syslog = false;
static volatile sig_atomic_t running = 1;

// Track current USB card number for disconnect verification
static char current_usb_card[16] = "";

static void audiomon_log(const char* msg) {
	if (use_syslog)
		syslog(LOG_INFO, "%s", msg);
	else
		printf("%s\n", msg);
}

static void write_audio_file(const char* device_identifier, enum DeviceType type) {
	mkdir(USERDATA_PATH, 0755);

	FILE* f = fopen(AUDIO_FILE, "w");
	if (!f) {
		audiomon_log("Failed to write audio config file");
		return;
	}

	if (type == DEVICE_BLUETOOTH) {
		fprintf(f,
				"defaults.bluealsa.device \"%s\"\n\n"
				"pcm.!default {\n"
				"    type plug\n"
				"    slave.pcm {\n"
				"        type bluealsa\n"
				"        device \"%s\"\n"
				"        profile \"a2dp\"\n"
				"        delay 0\n"
				"    }\n"
				"}\n"
				"ctl.!default {\n"
				"    type bluealsa\n"
				"}\n",
				device_identifier, device_identifier);

		char log_buf[256];
		snprintf(log_buf, sizeof(log_buf), "Updated .asoundrc with Bluetooth device: %s", device_identifier);
		audiomon_log(log_buf);
	} else if (type == DEVICE_USB_AUDIO) {
		fprintf(f,
				"pcm.!default {\n"
				"    type plug\n"
				"    slave.pcm \"hw:%s,0\"\n"
				"}\n"
				"ctl.!default {\n"
				"    type hw\n"
				"    card %s\n"
				"}\n",
				device_identifier, device_identifier);

		char log_buf[256];
		snprintf(log_buf, sizeof(log_buf), "Updated .asoundrc with USB audio device: %s", device_identifier);
		audiomon_log(log_buf);
	}

	fclose(f);

	// Ensure it's flushed to disk
	int fd = open(AUDIO_FILE, O_WRONLY);
	if (fd >= 0) {
		fsync(fd);
		close(fd);
	}
}

static void clear_audio_file(void) {
	if (unlink(AUDIO_FILE) == 0) {
		audiomon_log("Removed audio config");
		// Sync directory to ensure deletion is persisted
		int dfd = open(USERDATA_PATH, O_DIRECTORY);
		if (dfd >= 0) {
			fsync(dfd);
			close(dfd);
		}
	} else if (errno != ENOENT) {
		audiomon_log("Failed to remove audio config file");
	}
}

// Extract MAC address from D-Bus object path
// Path format: .../dev_AA_BB_CC_DD_EE_FF/...
// Returns length written to out (excluding NUL), or 0 on failure
static int path_to_mac(const char* path, char* out, size_t out_size) {
	const char* p = strstr(path, "dev_");
	if (!p || out_size < 18)
		return 0;

	p += 4; // skip "dev_"

	// Copy up to 17 chars (AA_BB_CC_DD_EE_FF), replacing '_' with ':'
	int i = 0;
	while (*p && *p != '/' && i < 17) {
		out[i] = (*p == '_') ? ':' : *p;
		i++;
		p++;
	}
	out[i] = '\0';
	return i;
}

// Extract ALSA card number from udev device
// Checks devnode (e.g. /dev/snd/controlC1) and SOUND_CARD property
static const char* get_usb_audio_card_number(struct udev_device* dev) {
	const char* devnode = udev_device_get_devnode(dev);
	if (devnode) {
		const char* p = strstr(devnode, "controlC");
		if (p)
			return p + 8; // number after "controlC"
	}

	// Fallback: check ALSA card property
	const char* card = udev_device_get_property_value(dev, "SOUND_CARD");
	if (card)
		return card;

	return NULL;
}

// Check if a udev device is a USB audio device
// check_devnode: if true, require controlC* devnode (for "add" events)
//                if false, skip devnode check (for "remove" events where node is gone)
static bool is_usb_audio_device(struct udev_device* dev, bool check_devnode) {
	const char* subsystem = udev_device_get_subsystem(dev);
	if (!subsystem || strcmp(subsystem, "sound") != 0)
		return false;

	if (check_devnode) {
		const char* devnode = udev_device_get_devnode(dev);
		if (!devnode || !strstr(devnode, "controlC"))
			return false;
	}

	// Must be USB-connected
	const char* devpath = udev_device_get_devpath(dev);
	return devpath && strstr(devpath, "usb") != NULL;
}

static bool has_uuid(DBusConnection* conn, const char* path, const char* uuid) {
	DBusMessage* msg = dbus_message_new_method_call(
		"org.bluez", path, "org.freedesktop.DBus.Properties", "Get");
	if (!msg)
		return false;

	const char* iface = "org.bluez.Device1";
	const char* prop = "UUIDs";
	dbus_message_append_args(msg,
							 DBUS_TYPE_STRING, &iface,
							 DBUS_TYPE_STRING, &prop,
							 DBUS_TYPE_INVALID);

	DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 1000, NULL);
	dbus_message_unref(msg);
	if (!reply)
		return false;

	DBusMessageIter iter;
	dbus_message_iter_init(reply, &iter);
	DBusMessageIter variant;
	dbus_message_iter_recurse(&iter, &variant);

	if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY) {
		dbus_message_unref(reply);
		return false;
	}

	DBusMessageIter array;
	dbus_message_iter_recurse(&variant, &array);

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
		const char* val;
		dbus_message_iter_get_basic(&array, &val);
		if (strcmp(val, uuid) == 0) {
			dbus_message_unref(reply);
			return true;
		}
		dbus_message_iter_next(&array);
	}

	dbus_message_unref(reply);
	return false;
}

static void handle_bt_connected(DBusConnection* conn, const char* path) {
	char mac[18];
	if (!path_to_mac(path, mac, sizeof(mac)))
		return;

	if (has_uuid(conn, path, UUID_A2DP)) {
		char log_buf[256];
		snprintf(log_buf, sizeof(log_buf), "Audio device connected: %s", mac);
		audiomon_log(log_buf);
		write_audio_file(mac, DEVICE_BLUETOOTH);
		SetAudioSink(AUDIO_SINK_BLUETOOTH);

		// Set BT A2DP mixer to max for software volume control
		system("amixer scontrols 2>/dev/null | grep -i 'A2DP' | "
			   "sed \"s/.*'\\([^']*\\)'.*/\\1/\" | "
			   "while read ctrl; do amixer sset \"$ctrl\" 127 2>/dev/null; done");
		audiomon_log("Set BT A2DP mixer volume to max");

		// Apply user's saved volume immediately
		SetVolume(GetVolume());
	}
}

static void handle_bt_disconnected(DBusConnection* conn, const char* path) {
	char mac[18];
	if (!path_to_mac(path, mac, sizeof(mac)))
		return;

	if (has_uuid(conn, path, UUID_A2DP)) {
		char log_buf[256];
		snprintf(log_buf, sizeof(log_buf), "Audio device disconnected: %s", mac);
		audiomon_log(log_buf);
		clear_audio_file();
		SetAudioSink(AUDIO_SINK_DEFAULT);
	}
}

static void handle_usb_audio_connected(struct udev_device* dev) {
	const char* card = get_usb_audio_card_number(dev);
	if (!card)
		return;

	// Store current card number for disconnect verification
	snprintf(current_usb_card, sizeof(current_usb_card), "%s", card);

	char log_buf[256];
	snprintf(log_buf, sizeof(log_buf), "USB audio device connected: card %s", card);
	audiomon_log(log_buf);
	write_audio_file(card, DEVICE_USB_AUDIO);
	SetAudioSink(AUDIO_SINK_USBDAC);

	// Set USB DAC mixer controls to 100%
	char cmd[512];
	snprintf(cmd, sizeof(cmd),
			 "amixer -c %s sset PCM 100%% 2>/dev/null; "
			 "amixer -c %s sset Master 100%% 2>/dev/null; "
			 "amixer -c %s sset Speaker 100%% 2>/dev/null; "
			 "amixer -c %s sset Headphone 100%% 2>/dev/null; "
			 "amixer -c %s sset Headset 100%% 2>/dev/null",
			 card, card, card, card, card);
	system(cmd);
	audiomon_log("Set USB DAC mixer volume to 100%");

	// Apply user's saved volume to the new DAC immediately
	SetVolume(GetVolume());
}

static void handle_usb_audio_disconnected(void) {
	if (current_usb_card[0] == '\0')
		return; // already handled
	audiomon_log("USB audio device disconnected");
	current_usb_card[0] = '\0';
	clear_audio_file();
	SetAudioSink(AUDIO_SINK_DEFAULT);
}

static void signal_handler(int sig) {
	(void)sig;
	running = 0;
}

static void scan_existing_usb_audio_devices(struct udev* udev) {
	audiomon_log("Scanning for existing USB audio devices...");

	struct udev_enumerate* enumerate = udev_enumerate_new(udev);
	if (!enumerate) {
		audiomon_log("Failed to create udev enumerator");
		return;
	}

	udev_enumerate_add_match_subsystem(enumerate, "sound");
	udev_enumerate_scan_devices(enumerate);

	struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
	struct udev_list_entry* entry;

	udev_list_entry_foreach(entry, devices) {
		const char* path = udev_list_entry_get_name(entry);
		struct udev_device* dev = udev_device_new_from_syspath(udev, path);

		if (dev) {
			if (is_usb_audio_device(dev, true))
				handle_usb_audio_connected(dev);
			udev_device_unref(dev);
		}
	}

	udev_enumerate_unref(enumerate);
	audiomon_log("Finished scanning for existing USB audio devices");
}

// Process D-Bus PropertiesChanged signals for Bluetooth device connect/disconnect
static void process_dbus_events(DBusConnection* conn) {
	dbus_connection_read_write(conn, 0);

	DBusMessage* msg;
	while ((msg = dbus_connection_pop_message(conn)) != NULL) {
		if (!dbus_message_is_signal(msg, "org.freedesktop.DBus.Properties", "PropertiesChanged")) {
			dbus_message_unref(msg);
			continue;
		}

		const char* path = dbus_message_get_path(msg);
		if (!path || !strstr(path, "dev_")) {
			dbus_message_unref(msg);
			continue;
		}

		DBusMessageIter args;
		dbus_message_iter_init(msg, &args);

		const char* iface = NULL;
		dbus_message_iter_get_basic(&args, &iface);
		if (!iface || strcmp(iface, "org.bluez.Device1") != 0) {
			dbus_message_unref(msg);
			continue;
		}

		dbus_message_iter_next(&args);
		if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) {
			dbus_message_unref(msg);
			continue;
		}

		DBusMessageIter changed;
		dbus_message_iter_recurse(&args, &changed);

		while (dbus_message_iter_get_arg_type(&changed) == DBUS_TYPE_DICT_ENTRY) {
			DBusMessageIter dict;
			dbus_message_iter_recurse(&changed, &dict);

			const char* key;
			dbus_message_iter_get_basic(&dict, &key);

			if (strcmp(key, "Connected") == 0) {
				dbus_message_iter_next(&dict);
				DBusMessageIter variant;
				dbus_message_iter_recurse(&dict, &variant);
				dbus_bool_t connected;
				dbus_message_iter_get_basic(&variant, &connected);

				if (connected)
					handle_bt_connected(conn, path);
				else
					handle_bt_disconnected(conn, path);
			}

			dbus_message_iter_next(&changed);
		}

		dbus_message_unref(msg);
	}
}

// Process udev events for USB audio device add/remove
static void process_udev_events(struct udev_monitor* mon) {
	struct udev_device* dev;
	// Drain all pending udev events (debounce: USB plug generates multiple)
	bool saw_add = false;
	bool saw_remove = false;
	struct udev_device* add_dev = NULL;

	while ((dev = udev_monitor_receive_device(mon)) != NULL) {
		const char* action = udev_device_get_action(dev);
		const char* subsystem = udev_device_get_subsystem(dev);

		if (subsystem && strcmp(subsystem, "sound") == 0 && action) {
			if (strcmp(action, "add") == 0 && is_usb_audio_device(dev, true)) {
				saw_add = true;
				// Keep the last valid add device (unref any previous)
				if (add_dev)
					udev_device_unref(add_dev);
				add_dev = dev;
				dev = NULL; // don't unref below
			} else if (strcmp(action, "remove") == 0 && is_usb_audio_device(dev, false)) {
				saw_remove = true;
			}
		}

		if (dev)
			udev_device_unref(dev);
	}

	// Process once after draining all events.
	// Brief delay lets the ALSA card finish initializing (kernel events arrive
	// before udevd rules run, so mixer controls may not be ready immediately).
	if (saw_add && add_dev) {
		usleep(500000); // 500ms
		handle_usb_audio_connected(add_dev);
	}
	if (saw_remove && !saw_add)
		handle_usb_audio_disconnected();

	if (add_dev)
		udev_device_unref(add_dev);
}

int main(int argc, char* argv[]) {
	if (argc > 1 && strcmp(argv[1], "-s") == 0) {
		use_syslog = true;
		openlog("audiomon", LOG_PID | LOG_CONS, LOG_USER);
	}

	InitSettings();
	SetAudioSink(AUDIO_SINK_DEFAULT);

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	// Initialize D-Bus (optional — needed for Bluetooth, not for USB audio)
	// IMPORTANT: Must use dbus_connection_open_private + manual register instead of
	// dbus_bus_get, because dbus_bus_get internally registers on the bus and if the
	// connection is rejected/dropped during registration, the default exit-on-disconnect
	// handler calls exit(1) before we can disable it.
	DBusError err;
	dbus_error_init(&err);
	DBusConnection* conn = NULL;

	const char* bus_addr = getenv("DBUS_SYSTEM_BUS_ADDRESS");
	if (!bus_addr)
		bus_addr = "unix:path=/var/run/dbus/system_bus_socket";

	conn = dbus_connection_open_private(bus_addr, &err);
	if (conn) {
		// Disable exit-on-disconnect BEFORE registering on the bus
		dbus_connection_set_exit_on_disconnect(conn, FALSE);
		if (!dbus_bus_register(conn, &err)) {
			audiomon_log("D-Bus register failed — Bluetooth audio monitoring disabled");
			if (dbus_error_is_set(&err))
				dbus_error_free(&err);
			dbus_connection_close(conn);
			dbus_connection_unref(conn);
			conn = NULL;
		}
	} else {
		audiomon_log("D-Bus unavailable — Bluetooth audio monitoring disabled, USB audio still active");
		if (dbus_error_is_set(&err))
			dbus_error_free(&err);
	}

	if (conn) {
		audiomon_log("Connected to system D-Bus");
		dbus_bus_add_match(conn,
						   "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
						   NULL);
		dbus_connection_flush(conn);
	}

	// Initialize udev
	struct udev* udev = udev_new();
	if (!udev) {
		audiomon_log("Failed to create udev context");
		return 1;
	}

	// Use "kernel" events — device nodes (/dev/snd/*) are created by devtmpfs before
	// the event fires. "udev" events depend on udevd processing rules for the sound
	// subsystem, which doesn't happen on all platforms (e.g. TG5040).
	struct udev_monitor* mon = udev_monitor_new_from_netlink(udev, "kernel");
	if (!mon) {
		audiomon_log("Failed to create udev monitor");
		udev_unref(udev);
		return 1;
	}

	// NOTE: Don't use udev_monitor_filter_add_match_subsystem_devtype() here.
	// On older libudev (e.g. 1.6.3 / kernel 4.9), the BPF filter doesn't work
	// correctly with "kernel" source and silently drops all events.
	// We filter manually in process_udev_events() instead.
	udev_monitor_enable_receiving(mon);

	// Scan for existing USB audio devices before starting event monitoring
	scan_existing_usb_audio_devices(udev);

	int udev_fd = udev_monitor_get_fd(mon);
	int dbus_fd = -1;

	if (conn && !dbus_connection_get_unix_fd(conn, &dbus_fd)) {
		audiomon_log("Warning: Could not get D-Bus file descriptor, will use polling");
		dbus_fd = -1;
	}

	audiomon_log(conn ? "Monitoring for Bluetooth and USB audio device events"
					  : "Monitoring for USB audio device events (no D-Bus)");

	while (running) {
		fd_set readfds;
		FD_ZERO(&readfds);

		if (dbus_fd >= 0)
			FD_SET(dbus_fd, &readfds);
		FD_SET(udev_fd, &readfds);

		int max_fd = (dbus_fd > udev_fd) ? dbus_fd : udev_fd;

		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		int ret = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			audiomon_log("select() error");
			break;
		}

		if (conn && dbus_fd >= 0 && FD_ISSET(dbus_fd, &readfds))
			process_dbus_events(conn);

		if (FD_ISSET(udev_fd, &readfds))
			process_udev_events(mon);
	}

	// Cleanup (private connection must be closed before unref)
	if (conn) {
		dbus_connection_close(conn);
		dbus_connection_unref(conn);
	}
	udev_monitor_unref(mon);
	udev_unref(udev);
	QuitSettings();

	if (use_syslog)
		closelog();
	return 0;
}
