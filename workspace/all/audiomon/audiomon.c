// audiomon.c
// Monitors Bluetooth device connections and USB-C DAC connections, updating .asoundrc for audio sinks

#include <dbus/dbus.h>
#include <libudev.h>
#include <alsa/asoundlib.h>
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
#include <time.h>

#include "msettings.h"
#include "defines.h"
#include "spk_guard.h"

#define AUDIO_FILE USERDATA_PATH "/.asoundrc"
#define UUID_A2DP "0000110b-0000-1000-8000-00805f9b34fb"

enum DeviceType {
	DEVICE_BLUETOOTH,
	DEVICE_USB_AUDIO
};

static bool use_syslog = false;
static volatile sig_atomic_t running = 1;

// Forward declaration for use in publish functions
static void audiomon_log(const char* msg);

// Track current USB card number for disconnect verification
static char current_usb_card[16] = "";

// Track current BT device for sink-state publishing
static char current_bt_mac[18] = "";
static volatile sig_atomic_t republish_requested = 0;
static volatile sig_atomic_t game_volume_requested = 0;
static int active_rate = 48000;
static int applied_game_volume = -1;
static int logged_game_volume_error = -1;

// Mirrors what write_audio_file()/clear_audio_file() last routed —
// publish must reflect actual routing, which is last-event-wins.
enum { ROUTED_DEFAULT,
	   ROUTED_BLUETOOTH,
	   ROUTED_USB } current_route = ROUTED_DEFAULT;

// Sink state published for apps/scripts (see DEV docs: sink=, rates=, card=)
#define SINK_STATE_FILE "/tmp/nx_audio_sink"
#define SINK_STATE_TMP "/tmp/.nx_audio_sink.tmp"

// ---- Speaker anti-pop sequencer (Smart Pro S / tg5050) ----------------------
// The codec powers its output stage up whenever a PCM stream starts and down
// within <1s of the last one closing. The real amp gate is the TrimUI kernel
// node /sys/class/speaker/mute (write 1 = force the amp OFF, write 0 = amp ON);
// it overrides the DAPM "SPK Switch" in both directions, so the switch is
// irrelevant to us. Two things pop, both verified on hardware with a game
// playing: (1) the codec powering UP while the amp is enabled (node 0); (2)
// enabling the amp (node 0) while the DAC is already pushing live signal.
// Powering DOWN, and enabling the amp into a MUTED DAC, are both silent. So per
// power-up we run: DAC_MUTE ("DAC Volume" 0, value saved) -> wait 350 ms for the
// codec power-up to settle -> AMP_ON (node 0, into silence) -> wait 150 ms for
// the amp to settle -> DAC_RESTORE (write the saved volume back in one step). On
// power-down the amp is forced off (node 1) and the DAC restored (both silent).
// At user volume 0 the amp is left muted (kills the idle hiss). Every poll we
// also re-assert the node against stray writers (old binaries calling
// PLAT_overrideMute, kernel resume re-init), rewriting only on mismatch.
// libmsettings SetRawVolume no longer touches the node (it drives "DAC Volume"
// only) and PLAT_overrideMute is a no-op on this platform, so audiomon owns it.
//
// PM State lives in the codec's DAPM widget dump (cheap to read).
static const char* SPK_PM_PATH =
	"/sys/devices/platform/soc@3000000/soc@3000000:codec_mach/"
	"sunxi-snd-plat-aaudio-sunxi-snd-codec/dapm_widget";
// The amp mute pin (1 = amp forced off, 0 = amp on).
static const char* SPK_MUTE_PATH = "/sys/class/speaker/mute";

static bool spk_guard_active = false;
static SpkGuard spk_guard;
static snd_ctl_t* spk_ctl = NULL;			 // kept open for the daemon's life
static snd_ctl_elem_id_t* spk_dac_id = NULL; // resolved "DAC Volume" id (numid)
static unsigned int spk_dac_count = 1;		 // "DAC Volume" channel count
static bool spk_dac_logged_error = false;
static bool spk_mute_logged_error = false;

static long long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 1 = codec output stage powered On, 0 = Off, -1 = read error (treated by the
// state machine as "no change").
static int spk_read_pm_on(void) {
	FILE* f = fopen(SPK_PM_PATH, "r");
	if (!f)
		return -1;
	int result = -1;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char* p = strstr(line, "PM State:");
		if (p) {
			result = strstr(p, "On") ? 1 : 0;
			break;
		}
	}
	fclose(f);
	return result;
}

// The amp gate is a rare-write sysfs node — open/write/close each time.
// mute=1 forces the amp off, mute=0 enables it.
static int spk_mute_read(void) {
	FILE* f = fopen(SPK_MUTE_PATH, "r");
	if (!f)
		return -1;
	int v = -1;
	if (fscanf(f, "%d", &v) != 1)
		v = -1;
	fclose(f);
	return v;
}

static void spk_mute_write(int mute) {
	FILE* f = fopen(SPK_MUTE_PATH, "w");
	if (!f) {
		if (!spk_mute_logged_error) {
			audiomon_log("Speaker guard: failed to open speaker mute node");
			spk_mute_logged_error = true;
		}
		return;
	}
	fprintf(f, "%d", mute ? 1 : 0);
	fclose(f);
	spk_mute_logged_error = false;
}

// Read "DAC Volume" (channel 0): current codec digital volume, or -1 on error.
static int spk_dac_read(void) {
	if (!spk_ctl || !spk_dac_id)
		return -1;
	snd_ctl_elem_value_t* v;
	snd_ctl_elem_value_alloca(&v);
	snd_ctl_elem_value_set_id(v, spk_dac_id);
	if (snd_ctl_elem_read(spk_ctl, v) < 0)
		return -1;
	return snd_ctl_elem_value_get_integer(v, 0);
}

// Write the same value to every "DAC Volume" channel.
static void spk_dac_write(int val) {
	if (!spk_ctl || !spk_dac_id)
		return;
	snd_ctl_elem_value_t* v;
	snd_ctl_elem_value_alloca(&v);
	snd_ctl_elem_value_set_id(v, spk_dac_id);
	for (unsigned int i = 0; i < spk_dac_count; i++)
		snd_ctl_elem_value_set_integer(v, i, val);
	if (snd_ctl_elem_write(spk_ctl, v) < 0) {
		if (!spk_dac_logged_error) {
			audiomon_log("Speaker guard: failed to write DAC Volume");
			spk_dac_logged_error = true;
		}
	} else {
		spk_dac_logged_error = false;
	}
}

static void spk_guard_apply(SpkGuardAction action) {
	switch (action) {
	case SPK_GUARD_DAC_MUTE:
		spk_dac_write(0); // the step already saved the user's level
		break;
	case SPK_GUARD_AMP_ON:
		spk_mute_write(0); // amp on, into the muted DAC
		break;
	case SPK_GUARD_DAC_RESTORE:
		spk_dac_write(spk_guard.saved_dac);
		break;
	case SPK_GUARD_AMP_OFF:
		spk_mute_write(1); // force the amp off
		break;
	case SPK_GUARD_NONE:
	default:
		break;
	}
}

// Activate only on tg5050 when the codec power sysfs, the amp mute node, and
// the "DAC Volume" control are all present (the card index moves, so name the
// card "audiocodec"). Keeps the ctl handle and the resolved id open for life.
static void spk_guard_setup(void) {
	if (strcmp(PLATFORM, "tg5050") != 0) {
		audiomon_log("Speaker guard inactive: not tg5050");
		return;
	}
	struct stat st;
	if (stat(SPK_PM_PATH, &st) != 0) {
		audiomon_log("Speaker guard inactive: codec power sysfs not found");
		return;
	}
	if (stat(SPK_MUTE_PATH, &st) != 0) {
		audiomon_log("Speaker guard inactive: speaker mute node not found");
		return;
	}
	if (snd_ctl_open(&spk_ctl, "hw:CARD=audiocodec", 0) < 0) {
		spk_ctl = NULL;
		audiomon_log("Speaker guard inactive: cannot open audiocodec control");
		return;
	}
	// Resolve "DAC Volume" (the codec digital volume, INTEGER) — muted before
	// the amp is enabled, restored after.
	snd_ctl_elem_info_t* info;
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_id_malloc(&spk_dac_id);
	snd_ctl_elem_id_set_interface(spk_dac_id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(spk_dac_id, "DAC Volume");
	snd_ctl_elem_info_set_id(info, spk_dac_id);
	if (snd_ctl_elem_info(spk_ctl, info) < 0) {
		audiomon_log("Speaker guard inactive: DAC Volume control not found");
		snd_ctl_elem_id_free(spk_dac_id);
		spk_dac_id = NULL;
		snd_ctl_close(spk_ctl);
		spk_ctl = NULL;
		return;
	}
	snd_ctl_elem_info_get_id(info, spk_dac_id); // adopt the numid-resolved id
	spk_dac_count = snd_ctl_elem_info_get_count(info);
	if (spk_dac_count < 1)
		spk_dac_count = 1;

	spk_guard_init(&spk_guard);
	spk_guard_active = true;
	audiomon_log("Speaker guard active");
	// First step forces the amp muted (node 1) before the boot priming stream,
	// so that stream powers the codec with the amp off (silent).
	spk_guard_apply(spk_guard_step(&spk_guard, spk_read_pm_on(), spk_dac_read(), now_ms()));
}

static void spk_guard_poll(void) {
	int pm = spk_read_pm_on();
	int dac = spk_dac_read();
	spk_guard_apply(spk_guard_step(&spk_guard, pm, dac, now_ms()));
	// Re-assert the amp node against stray writers. Wanted: 0 (amp on) only
	// while the guard has the amp enabled, else 1 (forced off). Rewrite only on
	// mismatch, so no log spam / needless writes.
	int wanted = (spk_guard.amp_on == 1) ? 0 : 1;
	int cur = spk_mute_read();
	if (cur >= 0 && cur != wanted)
		spk_mute_write(wanted);
}

// Parse one integer key from the shared NX Redux settings file.
// audiomon doesn't link common/config.c, so read the key=value line directly.
static int read_cfg_int(const char* key, int fallback) {
	char path[512];
	snprintf(path, sizeof(path), "%s/minuisettings.txt", SHARED_USERDATA_PATH);
	FILE* f = fopen(path, "r");
	if (!f)
		return fallback;
	char line[256];
	char pattern[64];
	snprintf(pattern, sizeof(pattern), "%s=%%i", key);
	int value = fallback;
	while (fgets(line, sizeof(line), f)) {
		int v;
		if (sscanf(line, pattern, &v) == 1) {
			value = v;
			break;
		}
	}
	fclose(f);
	return value;
}

// A2DP negotiated rate: the raw bluealsa PCM (not via plug) constrains
// rate min==max==negotiated, so hw-params report exactly it.
static int probe_bluetooth_rate(const char* mac) {
	char pcm_name[64];
	snprintf(pcm_name, sizeof(pcm_name), "bluealsa:DEV=%s,PROFILE=a2dp", mac);
	snd_pcm_t* pcm = NULL;
	if (snd_pcm_open(&pcm, pcm_name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0)
		return 44100; // conservative SBC default
	int rate = 44100;
	snd_pcm_hw_params_t* params;
	snd_pcm_hw_params_alloca(&params);
	if (snd_pcm_hw_params_any(pcm, params) >= 0) {
		unsigned int r = 0;
		int dir = 0;
		// bluealsa constrains min==max==negotiated once the transport is fully
		// up; an unconstrained result (UINT_MAX, seen when probing mid-A2DP
		// handshake) must not leak into the published state
		if (snd_pcm_hw_params_get_rate_max(params, &r, &dir) >= 0 &&
			r >= 8000 && r <= 192000)
			rate = (int)r;
	}
	snd_pcm_close(pcm);
	return rate;
}

// Supported subset of the standard rate ladder, publish order 48000-first.
static int probe_usb_rates(const char* card, int* rates_out, int max_rates) {
	static const unsigned int ladder[] = {48000, 44100, 88200, 96000, 176400, 192000};
	char pcm_name[32];
	snprintf(pcm_name, sizeof(pcm_name), "hw:%s,0", card);
	snd_pcm_t* pcm = NULL;
	if (snd_pcm_open(&pcm, pcm_name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0)
		return 0;
	int count = 0;
	snd_pcm_hw_params_t* params;
	snd_pcm_hw_params_alloca(&params);
	if (snd_pcm_hw_params_any(pcm, params) >= 0) {
		for (unsigned int i = 0; i < sizeof(ladder) / sizeof(ladder[0]) && count < max_rates; i++) {
			if (snd_pcm_hw_params_test_rate(pcm, params, ladder[i], 0) == 0)
				rates_out[count++] = (int)ladder[i];
		}
	}
	snd_pcm_close(pcm);
	return count;
}

// Publish /tmp/nx_audio_sink (atomic: tmp + rename). Probe failures fall back
// to conservative defaults; publishing must never block or fail routing.
static void publish_sink_state(void) {
	FILE* f = fopen(SINK_STATE_TMP, "w");
	if (!f) {
		audiomon_log("Failed to write sink state file");
		return;
	}

	bool negotiate = read_cfg_int("audioRateNegotiation", 1) != 0;

	switch (current_route) {
	case ROUTED_BLUETOOTH: {
		int rate = negotiate ? probe_bluetooth_rate(current_bt_mac) : 48000;
		int cap = read_cfg_int("btMaxRate", 48000);
		if (rate > cap)
			rate = cap;
		active_rate = rate;
		fprintf(f, "sink=bluetooth\nrate=%d\nrates=%d\nformat=S16_LE\n", rate, rate);
	} break;
	case ROUTED_USB: {
		int rates[8];
		/* dmix's slave must use an actually supported USB hardware rate;
		 * negotiation preference cannot turn this probe off. */
		int n = probe_usb_rates(current_usb_card, rates, 8);
		if (n == 0) {
			rates[0] = 48000;
			n = 1;
		}
		active_rate = rates[0];
		fprintf(f, "sink=usb\nrate=%d\nrates=", active_rate);
		for (int i = 0; i < n; i++)
			fprintf(f, "%s%d", i ? " " : "", rates[i]);
		fprintf(f, "\nformat=S16_LE\ncard=%s\n", current_usb_card);
	} break;
	case ROUTED_DEFAULT:
	default:
		active_rate = 48000;
		fprintf(f, "sink=default\nrate=48000\nrates=48000\nformat=S16_LE\n"); // dmix is fixed at 48 kHz
		break;
	}

	fclose(f);
	rename(SINK_STATE_TMP, SINK_STATE_FILE);
	audiomon_log("Published sink state");
}

static void audiomon_log(const char* msg) {
	if (use_syslog)
		syslog(LOG_INFO, "%s", msg);
	else
		printf("%s\n", msg);
}

static int clamp_game_volume(int value) {
	if (value < 0)
		return 0;
	if (value > 20)
		return 20;
	return value;
}

// Softvol controls are created lazily by ALSA. Prime only when a route is
// (re)configured; ordinary volume changes only update the existing control.
static bool apply_game_volume(const char* card, bool prime) {
	int volume = clamp_game_volume(GetGameVolume());
	if (prime) {
		applied_game_volume = -1;
		logged_game_volume_error = -1;
		char prime_command[256];
		snprintf(prime_command, sizeof(prime_command),
				 "dd if=/dev/zero bs=4096 count=1 2>/dev/null | aplay -q -D nx_game -f S16_LE -r %d -c 2 2>/dev/null",
				 active_rate);
		if (system(prime_command) != 0) {
			if (logged_game_volume_error != volume) {
				audiomon_log("Failed to prime Game Volume control");
				logged_game_volume_error = volume;
			}
			return false;
		}
	}

	char command[256];
	if (card && card[0])
		snprintf(command, sizeof(command), "amixer -c %s cset name='Game Volume' %d%% 2>/dev/null", card,
				 volume * 5);
	else
		snprintf(command, sizeof(command), "amixer cset name='Game Volume' %d%% 2>/dev/null", volume * 5);
	if (system(command) != 0) {
		if (logged_game_volume_error != volume) {
			audiomon_log("Failed to apply Game Volume control");
			logged_game_volume_error = volume;
		}
		return false;
	}
	logged_game_volume_error = -1;
	applied_game_volume = volume;
	return true;
}

static void apply_game_volume_if_changed(void) {
	int volume = clamp_game_volume(GetGameVolume());
	if (volume == applied_game_volume || current_route == ROUTED_BLUETOOTH)
		return;
	const char* card = current_route == ROUTED_USB ? current_usb_card : "audiocodec";
	(void)apply_game_volume(card, false);
}

static void write_default_audio_file(void) {
	mkdir(USERDATA_PATH, 0755);
	FILE* f = fopen(AUDIO_FILE, "w");
	if (!f) {
		audiomon_log("Failed to write audio config file");
		return;
	}
	// PlaybackDmix is the stock speaker dmix (ipc_key 1111, hw:audiocodec,0,
	// 48 kHz, period 2048, buffer 8192). Reuse it so OSD and NX clients share
	// the existing owner instead of creating a second hardware mixer.
	fprintf(f,
			"pcm.nx_game { type softvol; slave.pcm \"PlaybackDmix\"; control { name \"Game Volume\"; card audiocodec; } }\n"
			"pcm.nx_music { type plug; slave.pcm \"PlaybackDmix\"; }\n"
			"pcm.!default { type asym; playback.pcm \"nx_game\"; capture.pcm \"Capture\"; }\n"
			"ctl.!default { type hw; card audiocodec; }\n");
	fclose(f);
	active_rate = 48000;
	(void)apply_game_volume("audiocodec", true);
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
				"pcm.nx_bt_base { type plug; slave.pcm { type bluealsa; device \"%s\"; profile \"a2dp\"; delay 0; } }\n"
				"pcm.nx_music { type plug; slave.pcm \"nx_bt_base\"; }\n"
				"pcm.!default { type plug; slave.pcm \"nx_bt_base\"; }\n"
				"ctl.!default { type bluealsa; }\n",
				device_identifier, device_identifier);
		fflush(f);
		char log_buf[256];
		snprintf(log_buf, sizeof(log_buf), "Updated .asoundrc with Bluetooth device: %s", device_identifier);
		audiomon_log(log_buf);
	} else if (type == DEVICE_USB_AUDIO) {
		/* USB hardware is exclusive. Keep the plug safety net outside an ALSA
		 * dmix slave so concurrent music/gameplay streams share one negotiated
		 * hardware rate instead of racing direct hw opens. BlueALSA is left
		 * untouched: its one-client limitation cannot be solved with dmix. */
		fprintf(f,
				"pcm.nx_usb_dmix {\n"
				"    type dmix\n"
				"    ipc_key 0x4e5858\n"
				"    ipc_perm 0666\n"
				"    slave { pcm \"hw:%s,0\" rate %d format S16_LE channels 2 period_size 1024 periods 4 }\n"
				"}\n"
				"pcm.nx_game { type softvol; slave.pcm \"nx_usb_dmix\"; control { name \"Game Volume\"; card %s; } }\n"
				"pcm.nx_music { type plug; slave.pcm \"nx_usb_dmix\"; }\n"
				"pcm.!default { type plug; slave.pcm \"nx_game\"; }\n"
				"ctl.!default { type hw; card %s; }\n",
				device_identifier, active_rate, device_identifier, device_identifier);
		fflush(f);
		char log_buf[256];
		snprintf(log_buf, sizeof(log_buf), "Updated .asoundrc with USB audio device: %s", device_identifier);
		audiomon_log(log_buf);
	}

	fclose(f);

	if (type == DEVICE_USB_AUDIO)
		(void)apply_game_volume(device_identifier, true);

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

	// Connect can be signalled twice (Device1.Connected + MediaTransport1 added)
	if (current_route == ROUTED_BLUETOOTH && strcmp(mac, current_bt_mac) == 0)
		return;

	if (has_uuid(conn, path, UUID_A2DP)) {
		char log_buf[256];
		snprintf(log_buf, sizeof(log_buf), "Audio device connected: %s", mac);
		audiomon_log(log_buf);
		write_audio_file(mac, DEVICE_BLUETOOTH);
		snprintf(current_bt_mac, sizeof(current_bt_mac), "%s", mac);
		current_route = ROUTED_BLUETOOTH;
		publish_sink_state();
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
		write_default_audio_file();
		current_bt_mac[0] = '\0';
		current_route = ROUTED_DEFAULT;
		publish_sink_state();
		SetAudioSink(AUDIO_SINK_DEFAULT);
	}
}

// BlueZ removes org.bluez.MediaTransport1 when the A2DP stream link drops.
// Modern earbuds often keep an LE link (battery/status) alive, so
// Device1.Connected never flips false and the PropertiesChanged path above
// never fires — the transport object is the ground truth for audio.
static void handle_interfaces_removed(DBusMessage* msg) {
	DBusMessageIter args;
	if (!dbus_message_iter_init(msg, &args) ||
		dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
		return;

	const char* path = NULL;
	dbus_message_iter_get_basic(&args, &path);
	if (!path || !strstr(path, "dev_"))
		return;

	char mac[18];
	if (!path_to_mac(path, mac, sizeof(mac)))
		return;
	if (current_route != ROUTED_BLUETOOTH || strcmp(mac, current_bt_mac) != 0)
		return;

	if (!dbus_message_iter_next(&args) ||
		dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY)
		return;

	DBusMessageIter arr;
	dbus_message_iter_recurse(&args, &arr);
	while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
		const char* iface = NULL;
		dbus_message_iter_get_basic(&arr, &iface);
		if (iface && strcmp(iface, "org.bluez.MediaTransport1") == 0) {
			audiomon_log("A2DP transport removed (LE link may remain) - reverting audio route");
			clear_audio_file();
			write_default_audio_file();
			current_bt_mac[0] = '\0';
			current_route = ROUTED_DEFAULT;
			publish_sink_state();
			SetAudioSink(AUDIO_SINK_DEFAULT);
			return;
		}
		dbus_message_iter_next(&arr);
	}
}

// Mirror case: the A2DP transport can (re)appear without Device1.Connected
// changing (LE link stayed up the whole time). Route audio back to the device.
static void handle_interfaces_added(DBusConnection* conn, DBusMessage* msg) {
	DBusMessageIter args;
	if (!dbus_message_iter_init(msg, &args) ||
		dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
		return;

	const char* path = NULL;
	dbus_message_iter_get_basic(&args, &path);
	if (!path || !strstr(path, "dev_"))
		return;

	if (!dbus_message_iter_next(&args) ||
		dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY)
		return;

	bool has_transport = false;
	DBusMessageIter dict;
	dbus_message_iter_recurse(&args, &dict);
	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry;
		dbus_message_iter_recurse(&dict, &entry);
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
			const char* iface = NULL;
			dbus_message_iter_get_basic(&entry, &iface);
			if (iface && strcmp(iface, "org.bluez.MediaTransport1") == 0) {
				has_transport = true;
				break;
			}
		}
		dbus_message_iter_next(&dict);
	}
	if (!has_transport)
		return;

	// The transport lives at .../dev_XX_.../sepN/fdM — hand handle_bt_connected
	// the device object path (it queries Device1 properties on it).
	const char* p = strstr(path, "dev_");
	size_t prefix = (size_t)(p - path) + 4 + 17; // through the MAC segment
	char devpath[128];
	if (prefix >= sizeof(devpath) || strlen(path) < prefix)
		return;
	memcpy(devpath, path, prefix);
	devpath[prefix] = '\0';
	handle_bt_connected(conn, devpath);
}

static void handle_usb_audio_connected(struct udev_device* dev) {
	const char* card = get_usb_audio_card_number(dev);
	if (!card)
		return;

	// Store current card number for disconnect verification and select the
	// shared hardware rate before writing the dmix slave configuration.
	snprintf(current_usb_card, sizeof(current_usb_card), "%s", card);
	int rates[8];
	int rate_count = probe_usb_rates(card, rates, 8);
	active_rate = rate_count > 0 ? rates[0] : 48000;

	char log_buf[256];
	snprintf(log_buf, sizeof(log_buf), "USB audio device connected: card %s", card);
	audiomon_log(log_buf);
	write_audio_file(card, DEVICE_USB_AUDIO);
	current_route = ROUTED_USB;
	publish_sink_state();
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
	write_default_audio_file();
	current_route = ROUTED_DEFAULT;
	publish_sink_state();
	SetAudioSink(AUDIO_SINK_DEFAULT);
}

static void signal_handler(int sig) {
	if (sig == SIGUSR1) {
		republish_requested = 1;
		game_volume_requested = 1;
		return;
	}
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
		// A2DP transport lifecycle — the audio-truth signal (see handlers above)
		if (dbus_message_is_signal(msg, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved")) {
			handle_interfaces_removed(msg);
			dbus_message_unref(msg);
			continue;
		}
		if (dbus_message_is_signal(msg, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded")) {
			handle_interfaces_added(conn, msg);
			dbus_message_unref(msg);
			continue;
		}

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

				if (connected) {
					// Do NOT route yet: Connected covers any link, including an
					// LE-only one (battery/status) with no audio path behind it.
					// Routing happens when the A2DP MediaTransport1 appears —
					// see handle_interfaces_added.
					char mac[18];
					if (path_to_mac(path, mac, sizeof(mac)) && has_uuid(conn, path, UUID_A2DP)) {
						char log_buf[256];
						snprintf(log_buf, sizeof(log_buf),
								 "BT device connected: %s (waiting for A2DP transport)", mac);
						audiomon_log(log_buf);
					}
				} else {
					handle_bt_disconnected(conn, path);
				}
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
	// Initialise the speaker guard (and force the switch off) BEFORE the boot
	// priming stream in write_default_audio_file(), so that stream powers the
	// codec with the amp off — otherwise the cold power-up pops.
	spk_guard_setup();
	write_default_audio_file();
	SetAudioSink(AUDIO_SINK_DEFAULT);
	publish_sink_state();

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGUSR1, signal_handler);

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
		// A2DP transport lifecycle: earbuds can drop/re-establish the audio
		// link while their LE connection stays up, so Device1.Connected alone
		// is not a reliable audio signal (see handle_interfaces_removed/added)
		dbus_bus_add_match(conn,
						   "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded'",
						   NULL);
		dbus_bus_add_match(conn,
						   "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesRemoved'",
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
		if (republish_requested) {
			republish_requested = 0;
			publish_sink_state(); // settings page poked us after a policy change
		}
		if (game_volume_requested || GetGameVolume() != applied_game_volume) {
			game_volume_requested = 0;
			apply_game_volume_if_changed();
		}

		fd_set readfds;
		FD_ZERO(&readfds);

		if (dbus_fd >= 0)
			FD_SET(dbus_fd, &readfds);
		FD_SET(udev_fd, &readfds);

		int max_fd = (dbus_fd > udev_fd) ? dbus_fd : udev_fd;

		struct timeval timeout;
		if (spk_guard_active) {
			// Poll the codec power state at 50 ms so the ON fires promptly once
			// the 350 ms settle elapses and the OFF lands before the next
			// power-up. tg5040 has no such codec and keeps the 1 s idle.
			timeout.tv_sec = 0;
			timeout.tv_usec = 50000;
		} else {
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
		}

		int ret = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

		// Runs every iteration regardless of ret (timeout or fd ready).
		if (spk_guard_active)
			spk_guard_poll();

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

	// A stopped daemon can no longer manage the amp, so leave the speaker live:
	// enabling it now may pop on the next power-up, but that beats a dead
	// speaker until audiomon is restarted. Restore the DAC first so a
	// mid-sequence stop never leaves the volume muted.
	if (spk_guard_active) {
		if (spk_guard.dac_muted)
			spk_dac_write(spk_guard.saved_dac);
		spk_mute_write(0); // amp on
		if (spk_dac_id)
			snd_ctl_elem_id_free(spk_dac_id);
		if (spk_ctl)
			snd_ctl_close(spk_ctl);
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
