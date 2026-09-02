// desktop
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>

#include <msettings.h>

#include "defines.h"
#include "platform.h"
#include "api.h"
#include "utils.h"

#include "scaler.h"

#include "desktop_probe.h"

#include <dirent.h>

void PLAT_initInput(void) {
	// SDL_INIT_GAMECONTROLLER implies SDL_INIT_JOYSTICK. Controllers present now
	// arrive as SDL_CONTROLLERDEVICEADDED on the first event pump and are opened
	// in the shared poll loop (api.c, PAD_poll).
	//
	// Note: SDL_GameControllerOpen opens the underlying joystick regardless, and
	// SDL always emits BOTH the raw SDL_JOY* events and the translated
	// SDL_CONTROLLER* events for an opened controller — so NOT opening a second
	// raw SDL_Joystick here does not by itself prevent double input. It's
	// harmless only because desktop's JOY_*/AXIS_* are all -1 (CODE_NA), making
	// the shared SDL_JOYBUTTON/JOYAXIS branches inert — EXCEPT SDL_JOYHATMOTION,
	// which is ungated: a pad whose d-pad is reported as a hat raises both a
	// JOYHAT and a CONTROLLERBUTTON for the same BTN_DPAD_* bit. Today both paths
	// only do idempotent bitmask set/unset behind an "already pressed" guard, so
	// the overlap is a no-op; preserve that if the hat handler ever changes.
	SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
}
void PLAT_quitInput(void) {
	SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER); // closes any open controller
}

///////////////////////////////

void PLAT_getNetworkStatus(int* is_online) {
	*is_online = PLAT_wifiConnected();
}

void PLAT_getBatteryStatus(int* is_charging, int* charge) {
	PLAT_getBatteryStatusFine(is_charging, charge);
}

void PLAT_getBatteryStatusFine(int* is_charging, int* charge) {
	*is_charging = 1;
	*charge = 100;
}

int PLAT_isUSBConnected(void) {
	return 0; // not a USB gadget
}

void PLAT_enableBacklight(int enable) {
	// buh
}

void PLAT_powerOff(int reboot) {
	SND_quit();
	VIB_quit();
	PWR_quit();
	GFX_quit();
	exit(0);
}

///////////////////////////////

void PLAT_setCPUSpeed(int speed) {
	// buh
}

void PLAT_setCPUSpeedAuto(void) {
	// buh
}

void PLAT_setRumble(int strength) {
	// buh
}

char* PLAT_getModel(void) {
	return "Desktop";
}

// Shown as "OS version" on the Settings About page: the host OS release, not a
// device firmware string (desktop has no firmware).
void PLAT_getOsVersionInfo(char* output_str, size_t max_len) {
#ifdef __APPLE__
	FILE* p = popen("sw_vers -productVersion 2>/dev/null", "r");
	if (p) {
		char ver[64] = {0};
		char* got = fgets(ver, sizeof(ver), p);
		pclose(p);
		if (got) {
			trimTrailingNewlines(ver);
			if (ver[0]) {
				snprintf(output_str, max_len, "macOS %s", ver);
				return;
			}
		}
	}
#else
	FILE* f = fopen("/etc/os-release", "r");
	if (f) {
		char line[256];
		while (fgets(line, sizeof(line), f)) {
			if (strncmp(line, "PRETTY_NAME=", 12) != 0)
				continue;
			char* v = line + 12;
			trimTrailingNewlines(v);
			size_t len = strlen(v);
			if (len >= 2 && v[0] == '"' && v[len - 1] == '"') {
				v[len - 1] = '\0';
				v++;
			}
			if (v[0]) {
				snprintf(output_str, max_len, "%s", v);
				fclose(f);
				return;
			}
		}
		fclose(f);
	}
#endif
	struct utsname u;
	if (uname(&u) == 0)
		snprintf(output_str, max_len, "%s %s", u.sysname, u.release);
	else
		snprintf(output_str, max_len, "unknown");
}

ConnectionStrength PLAT_connectionStrength(void) {
	// Desktop has no RSSI to report -- reuse the cached reachability probe
	// (below) to at least make the menu-bar icon track real connectivity
	// instead of always reading "connected". PLAT_wifiEnabled() is always
	// true here, so there's no OFF case to represent.
	return PLAT_wifiConnected() ? SIGNAL_STRENGTH_HIGH : SIGNAL_STRENGTH_DISCONNECTED;
}

/////////////////////////////////
// Remove, just for debug


#define MAX_LINE_LENGTH 200
#define ZONE_PATH "/var/db/timezone/zoneinfo"
#define ZONE_TAB_PATH ZONE_PATH "/zone.tab"

static char cached_timezones[MAX_TIMEZONES][MAX_TZ_LENGTH];
static int cached_tz_count = -1;

int compare_timezones(const void* a, const void* b) {
	return strcmp((const char*)a, (const char*)b);
}

void PLAT_initTimezones() {
	if (cached_tz_count != -1) { // Already initialized
		return;
	}

	FILE* file = fopen(ZONE_TAB_PATH, "r");
	if (!file) {
		LOG_info("Error opening file %s\n", ZONE_TAB_PATH);
		return;
	}

	char line[MAX_LINE_LENGTH];
	cached_tz_count = 0;

	while (fgets(line, sizeof(line), file)) {
		// Skip comment lines
		if (line[0] == '#' || strlen(line) < 3) {
			continue;
		}

		char* token = strtok(line, "\t"); // Skip country code
		if (!token)
			continue;

		token = strtok(NULL, "\t"); // Skip latitude/longitude
		if (!token)
			continue;

		token = strtok(NULL, "\t\n"); // Extract timezone
		if (!token)
			continue;

		// Check for duplicates before adding
		int duplicate = 0;
		for (int i = 0; i < cached_tz_count; i++) {
			if (strcmp(cached_timezones[i], token) == 0) {
				duplicate = 1;
				break;
			}
		}

		if (!duplicate && cached_tz_count < MAX_TIMEZONES) {
			strncpy(cached_timezones[cached_tz_count], token, MAX_TZ_LENGTH - 1);
			cached_timezones[cached_tz_count][MAX_TZ_LENGTH - 1] = '\0'; // Ensure null-termination
			cached_tz_count++;
		}
	}

	fclose(file);

	// Sort the list alphabetically
	qsort(cached_timezones, cached_tz_count, MAX_TZ_LENGTH, compare_timezones);
}

void PLAT_getTimezones(char timezones[MAX_TIMEZONES][MAX_TZ_LENGTH], int* tz_count) {
	if (cached_tz_count == -1) {
		LOG_warn("Error: Timezones not initialized. Call PLAT_initTimezones first.\n");
		*tz_count = 0;
		return;
	}

	memcpy(timezones, cached_timezones, sizeof(cached_timezones));
	*tz_count = cached_tz_count;
}

char* PLAT_getCurrentTimezone() {
	// call readlink -f /tmp/localtime to get the current timezone path, and
	// then remove /usr/share/zoneinfo/ from the beginning of the path to get the timezone name.
	char* tz_path = (char*)malloc(256);
	if (!tz_path) {
		return NULL;
	}
	if (readlink("/etc/localtime", tz_path, 256) == -1) {
		free(tz_path);
		return NULL;
	}
	tz_path[255] = '\0'; // Ensure null-termination
	char* tz_name = strstr(tz_path, ZONE_PATH "/");
	if (tz_name) {
		tz_name += strlen(ZONE_PATH "/");
		return strdup(tz_name);
	} else {
		return strdup(tz_path);
	}
}

void PLAT_setCurrentTimezone(const char* tz) {
	return;
	if (cached_tz_count == -1) {
		LOG_warn("Error: Timezones not initialized. Call PLAT_initTimezones first.\n");
		return;
	}

	// tzset()

	// tz will be in format Asia/Shanghai
	char* tz_path = (char*)malloc(256);
	if (!tz_path) {
		return;
	}
	snprintf(tz_path, 256, ZONE_PATH "/%s", tz);
	if (unlink("/tmp/localtime") == -1) {
		LOG_error("Failed to remove existing symlink: %s\n", strerror(errno));
	}
	if (symlink(tz_path, "/tmp/localtime") == -1) {
		LOG_error("Failed to set timezone: %s\n", strerror(errno));
	}
	free(tz_path);
}

/////////////////////

void PLAT_wifiInit() {}
bool PLAT_hasWifi() {
	return true;
}
bool PLAT_wifiEnabled() {
	return true; // host always has a network stack
}
void PLAT_wifiEnable(bool on) {}

int PLAT_wifiScan(struct WIFI_network* networks, int max) {
	for (int i = 0; i < 5; i++) {
		struct WIFI_network* network = &networks[i];

		sprintf(network->ssid, "Network%d", i);
		strcpy(network->bssid, "01:01:01:01:01:01");
		network->rssi = (70 / 5) * (i + 1);
		network->freq = 2400;
		network->security = i % 2 ? SECURITY_WPA2_PSK : SECURITY_WEP;
	}
	return 5;
}
// Background-refresh reachability state. The menu bar polls PLAT_wifiConnected()
// every frame, so probing synchronously there would block the render thread for
// up to the probe's timeout on every cache miss (a real periodic UI hitch while
// offline, since a dropped SYN doesn't return quickly). Instead a single detached
// thread refreshes this value every 3s in the background, and the poll just reads
// the latest result -- instant, never blocks.
static _Atomic int g_wifi_reachable = 0;
static pthread_once_t g_wifi_probe_once = PTHREAD_ONCE_INIT;

static void* wifi_probe_thread(void* arg) {
	(void)arg;
	for (;;) {
		const char* host = getenv("NXREDUX_PROBE_HOST");
		const char* ports = getenv("NXREDUX_PROBE_PORT");
		int reachable = desktop_probe_reachable(host && *host ? host : "1.1.1.1",
												ports && *ports ? atoi(ports) : 53, 1000);
		atomic_store(&g_wifi_reachable, reachable);
		sleep(3);
	}
	return NULL;
}

static void wifi_probe_start(void) {
	pthread_t tid;
	if (pthread_create(&tid, NULL, wifi_probe_thread, NULL) == 0) {
		pthread_detach(tid);
	}
}

bool PLAT_wifiConnected() {
	// Ensure the background probe thread is running, then return its latest
	// result. First ~3s after startup may read offline until the thread's
	// first probe lands -- acceptable, bounded staleness.
	pthread_once(&g_wifi_probe_once, wifi_probe_start);
	return atomic_load(&g_wifi_reachable) != 0;
}
int PLAT_wifiConnection(struct WIFI_connection* connection_info) {
	connection_info->freq = 2400;
	strcpy(connection_info->ip, "127.0.0.1");
	strcpy(connection_info->ssid, "Network1");
	return 0;
}
bool PLAT_wifiHasCredentials(char* ssid, WifiSecurityType sec) {
	return false;
}
void PLAT_wifiForget(char* ssid, WifiSecurityType sec) {}
void PLAT_wifiConnect(char* ssid, WifiSecurityType sec) {}
void PLAT_wifiConnectPass(const char* ssid, WifiSecurityType sec, const char* pass) {}
void PLAT_wifiDisconnect() {}

/////////////////////////

// We use the generic video implementation here
#include "generic_video.c"