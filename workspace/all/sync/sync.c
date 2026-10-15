#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "ui_buttonhintbar.h"
#include "ui_list.h"
#include "ui_menubar.h"
#include "ui_message.h"
#include "ui_splash.h"
#include "ui_quitrequest.h"
#include "utils.h"
#include "sync_proto.h"
#include "usblink_state.h"

// Sync configuration
#define SYNC_UDP_PORT 19999
#define SYNC_RSYNC_PORT 18730
#define HELLO_MSG "HELLO_TRIMUI_SYNC:"
#define ACK_MSG "TRIMUI_SYNC_ACK:"
#define READY_MSG "TRIMUI_SYNC_READY"
#define DONE_MSG "TRIMUI_SYNC_DONE"
#define BROADCAST_INTERVAL_MS 1000
#define RSYNC_CONF_PATH "/tmp/rsyncd.conf"
#define RSYNC_PID_PATH "/tmp/rsyncd.pid"
#define USBLINK_STATE_PATH "/tmp/usblink.state"
#define USBLINK_PID_PATH "/tmp/usblink.pid"

// Paths to sync
// "<SDCARD_PATH>/Saves", built with snprintf.
static const char* saves_path(void) {
	static char buf[MAX_PATH];
	snprintf(buf, sizeof(buf), "%s/Saves", SDCARD_PATH);
	return buf;
}
#define SHARED_DATA_PATH SHARED_USERDATA_PATH
// ROMS_PATH already defined in defines.h

// rsync binary path (shared across platforms)
static const char* rsync_bin_path(void) {
	static char buf[MAX_PATH];
	snprintf(buf, sizeof(buf), "%s/rsync", SHARED_BIN_PATH);
	return buf;
}

// Single-quote a path for a shell command line, so a path carrying spaces or
// an apostrophe survives the shell; plain paths pass through unchanged.
static const char* shq(const char* path, char* buf, size_t size) {
	strncpy(buf, path, size - 1);
	buf[size - 1] = '\0';
	return escapeSingleQuotes(buf, size);
}

// Log buffer for terminal-like display
#define LOG_MAX_LINES 20
#define LOG_LINE_LEN 128

static char log_lines[LOG_MAX_LINES][LOG_LINE_LEN];
static int log_count = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// States
enum SyncState {
	STATE_NO_WIFI,
	STATE_READY,
	STATE_WAITING,
	STATE_SYNCING,
	STATE_ERROR,
	STATE_DONE,
};

static SDL_Surface* screen;
static enum SyncState state = STATE_READY;

// Network state
static char own_ip[64] = {0};
static char peer_ip[64] = {0};
static char broadcast_ip[64] = {0};
static volatile bool peer_found = false;
static volatile bool discovery_running = false;
static pthread_t discovery_thread;
static bool discovery_thread_active = false;
static int udp_sock = -1;

// Sync state
static volatile bool sync_cancel = false;
static volatile bool sync_done = false;
static volatile int sync_result = 0; // 0=success, -1=error
static volatile int sync_phase = 0;	 // 1-4 for progress display
static pthread_t sync_thread;
static bool sync_thread_active = false;
static bool is_server = false;

// Sync options
static volatile bool sync_roms = false;
static volatile bool peer_sync_roms = false;
// The peer must be the rsync client (a phone: its ROMs are not plain files).
static volatile bool peer_client_only = false;
// The peer filters .userdata/shared itself, so our daemon may filter too. Old
// firmware doesn't: its pushes would fail (exit 23) on refused files, so for
// it the daemon stays open, as before.
static volatile bool peer_filters = false;

// The link the user picked on the Ready screen; the cable is the default.
// Both devices must pick the same one.
static volatile bool via_cable = true;
// USB cable link (usblink.elf, shared with netplay): started only when the
// user picks the cable, stopped when that session ends unless it was already
// running before (someone else's).
static bool usblink_owned = false;

// Animation state
static int dot_count = 0;
static uint32_t last_dot_time = 0;

static void log_clear(void) {
	pthread_mutex_lock(&log_mutex);
	log_count = 0;
	pthread_mutex_unlock(&log_mutex);
}

static void log_add(const char* line) {
	pthread_mutex_lock(&log_mutex);

	// Shift lines up if buffer is full
	if (log_count >= LOG_MAX_LINES) {
		memmove(log_lines[0], log_lines[1], (LOG_MAX_LINES - 1) * LOG_LINE_LEN);
		log_count = LOG_MAX_LINES - 1;
	}

	// Copy line, truncate if needed
	strncpy(log_lines[log_count], line, LOG_LINE_LEN - 1);
	log_lines[log_count][LOG_LINE_LEN - 1] = '\0';

	// Strip trailing newline
	int len = strlen(log_lines[log_count]);
	if (len > 0 && log_lines[log_count][len - 1] == '\n')
		log_lines[log_count][len - 1] = '\0';

	log_count++;
	pthread_mutex_unlock(&log_mutex);
}

static int get_own_ip(void) {
	struct ifaddrs* ifaddr;
	struct ifaddrs* ifa;

	if (getifaddrs(&ifaddr) == -1)
		return -1;

	// Virtual interfaces (VPN tunnels, VM/container bridges, peer-to-peer
	// links). Advertising one of their addresses in the discovery payload
	// sends the peer's READY/rsync traffic somewhere unreachable — the peer
	// then waits forever.
	static const char* virtual_prefixes[] = {
		"utun", "bridge", "awdl", "llw", "vmnet", "docker",
		"veth", "tap", "tun", "zt", "p2p", "nxlink", NULL};

	int found = 0;
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;
		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if (!(ifa->ifa_flags & IFF_UP))
			continue;
		if (ifa->ifa_flags & IFF_LOOPBACK)
			continue;

		int is_virtual = 0;
		for (int v = 0; virtual_prefixes[v]; v++) {
			if (strncmp(ifa->ifa_name, virtual_prefixes[v],
						strlen(virtual_prefixes[v])) == 0) {
				is_virtual = 1;
				break;
			}
		}
		if (is_virtual)
			continue;

		struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
		inet_ntop(AF_INET, &sa->sin_addr, own_ip, sizeof(own_ip));

		if (!ifa->ifa_netmask)
			continue;
		struct sockaddr_in* mask = (struct sockaddr_in*)ifa->ifa_netmask;
		uint32_t ip_n = sa->sin_addr.s_addr;
		uint32_t mask_n = mask->sin_addr.s_addr;
		uint32_t bcast_n = ip_n | ~mask_n;
		struct in_addr bcast_addr;
		bcast_addr.s_addr = bcast_n;
		inet_ntop(AF_INET, &bcast_addr, broadcast_ip, sizeof(broadcast_ip));

		found = 1;

		// First real interface wins (wlan0 on devices).
		break;
	}

	freeifaddrs(ifaddr);
	return found ? 0 : -1;
}

static int create_send_socket(void) {
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return -1;

	int yes = 1;
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

	return sock;
}

static int create_recv_socket(void) {
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return -1;

	int yes = 1;
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
	setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SYNC_UDP_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(sock);
		return -1;
	}

	return sock;
}

static void send_udp_bytes(int sock, const char* msg, size_t len, const char* dest_ip) {
	struct sockaddr_in dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(SYNC_UDP_PORT);
	inet_pton(AF_INET, dest_ip, &dest.sin_addr);
	sendto(sock, msg, len, 0, (struct sockaddr*)&dest, sizeof(dest));
}

static void send_udp_message(int sock, const char* msg, const char* dest_ip) {
	send_udp_bytes(sock, msg, strlen(msg), dest_ip);
}

// The cable link's addresses while usblink reports it up, else false.
static bool cable_link(char* local, size_t local_size, char* peer, size_t peer_size) {
	UsbLinkState st;
	if (usblink_state_read(USBLINK_STATE_PATH, &st) != 0 || strcmp(st.link, "up") != 0)
		return false;
	if (!sync_ip_is_cable(st.local_ip) || !sync_ip_is_cable(st.peer_ip))
		return false;
	snprintf(local, local_size, "%s", st.local_ip);
	snprintf(peer, peer_size, "%s", st.peer_ip);
	return true;
}

// Brought up the way the netplay wizard does. `start` waits up to ~3 s for
// the daemon's first state, so it runs on the discovery thread; stopping joins
// that thread first, so a quick back can't stop the link before it started.
static void cable_link_start(void) {
	system("usblink.elf start >/dev/null 2>&1");
}

static void cable_link_stop(void) {
	if (usblink_owned)
		system("usblink.elf stop >/dev/null 2>&1");
	usblink_owned = false;
}

// The usblink error to show while waiting for the cable, or NULL.
static const char* cable_link_error(void) {
	UsbLinkState st;
	if (usblink_state_read(USBLINK_STATE_PATH, &st) != 0 || strcmp(st.link, "error") != 0)
		return NULL;
	return !strcmp(st.error, "version") ? "Both devices need the same\nNXRedux version."
										: "USB link unavailable\non this device.";
}

static bool wifi_ready(void) {
	int is_online = 0;
	PLAT_getNetworkStatus(&is_online);
	return is_online && get_own_ip() == 0;
}

// Discovery runs on the link the user picked. Over the cable it waits for
// usblink to report the link up, then sends HELLO straight to the peer's
// address; on Wi-Fi HELLO is broadcast. Packets from the other link are
// ignored.
static void* discovery_thread_func(void* arg) {
	(void)arg;
	char send_buf[128];
	char recv_buf[256];
	char cable_peer[16] = {0};

	if (via_cable)
		cable_link_start();

	uint32_t last_broadcast = SDL_GetTicks() - BROADCAST_INTERVAL_MS;

	while (discovery_running && !app_quit) {
		uint32_t now = SDL_GetTicks();
		if (now - last_broadcast >= BROADCAST_INTERVAL_MS) {
			last_broadcast = now;
			char local[16];
			bool cable = via_cable;
			if (cable) {
				if (cable_link(local, sizeof(local), cable_peer, sizeof(cable_peer)))
					snprintf(own_ip, sizeof(own_ip), "%s", local);
				else
					own_ip[0] = cable_peer[0] = '\0';
			} else if (get_own_ip() != 0) {
				own_ip[0] = '\0';
			}

			if (own_ip[0]) {
				// Rebuilt every time: it carries the current ROMs toggle.
				int n = sync_format_payload(send_buf, sizeof(send_buf), HELLO_MSG, own_ip, sync_roms);
				if (n > 0)
					send_udp_bytes(udp_sock, send_buf, n, cable ? cable_peer : broadcast_ip);
			}
		}

		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(udp_sock, &readfds);
		struct timeval tv = {0, 200000};

		int ret = select(udp_sock + 1, &readfds, NULL, NULL, &tv);
		if (ret <= 0 || !FD_ISSET(udp_sock, &readfds))
			continue;

		struct sockaddr_in from;
		socklen_t fromlen = sizeof(from);
		ssize_t n = recvfrom(udp_sock, recv_buf, sizeof(recv_buf) - 1, 0,
							 (struct sockaddr*)&from, &fromlen);
		if (n <= 0)
			continue;
		recv_buf[n] = '\0';

		bool hello = strncmp(recv_buf, HELLO_MSG, strlen(HELLO_MSG)) == 0;
		size_t prefix_len = hello ? strlen(HELLO_MSG) : strlen(ACK_MSG);
		if (!hello && strncmp(recv_buf, ACK_MSG, prefix_len) != 0)
			continue;

		SyncPeer sender;
		if (!sync_parse_payload(recv_buf + prefix_len, n - prefix_len, &sender))
			continue;

		// The packet must come from the address it advertises (it ends up in
		// rsync URLs and hosts allow), over the link being discovered on.
		char src_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &from.sin_addr, src_ip, sizeof(src_ip));
		if (strcmp(sender.ip, src_ip) != 0 || strcmp(sender.ip, own_ip) == 0)
			continue;
		if (!own_ip[0] || (via_cable ? strcmp(sender.ip, cable_peer) != 0 : sync_ip_is_cable(sender.ip)))
			continue;

		if (hello) {
			char ack_buf[128];
			int len = sync_format_payload(ack_buf, sizeof(ack_buf), ACK_MSG, own_ip, sync_roms);
			if (len > 0)
				send_udp_bytes(udp_sock, ack_buf, len, sender.ip);
		}

		if (!peer_found) {
			snprintf(peer_ip, sizeof(peer_ip), "%s", sender.ip);
			peer_sync_roms = sender.roms;
			peer_client_only = sender.client_only;
			peer_filters = sender.filters;
			__sync_synchronize();
			peer_found = true;
			discovery_running = false;
		}
	}

	return NULL;
}

static void start_discovery(void) {
	peer_found = false;
	peer_sync_roms = false;
	peer_client_only = false;
	peer_filters = false;
	peer_ip[0] = '\0';
	discovery_running = true;

	// Prevent sleep and power off during discovery and sync
	PWR_disableSleep();
	PWR_disableAutosleep();
	PWR_disablePowerOff();

	udp_sock = create_recv_socket();
	if (udp_sock < 0) {
		PWR_enableSleep();
		PWR_enableAutosleep();
		state = STATE_ERROR;
		return;
	}

	if (pthread_create(&discovery_thread, NULL, discovery_thread_func, NULL) != 0) {
		close(udp_sock);
		udp_sock = -1;
		PWR_enableSleep();
		PWR_enableAutosleep();
		state = STATE_ERROR;
		return;
	}
	discovery_thread_active = true;
	state = STATE_WAITING;
}

static void stop_discovery(void) {
	discovery_running = false;
	if (discovery_thread_active) {
		pthread_join(discovery_thread, NULL);
		discovery_thread_active = false;
	}
	if (udp_sock >= 0) {
		close(udp_sock);
		udp_sock = -1;
	}
	// Note: sleep is re-enabled later when sync completes/cancels/errors,
	// or immediately if discovery is stopped without syncing (B to exit).
}

static int wait_for_udp_message(const char* expected, int timeout_sec) {
	int sock = create_recv_socket();
	if (sock < 0)
		return -1;

	char buf[256];
	time_t start = time(NULL);

	while (!sync_cancel && !app_quit) {
		if (time(NULL) - start > timeout_sec) {
			close(sock);
			return -1;
		}

		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		struct timeval tv = {1, 0};

		int ret = select(sock + 1, &readfds, NULL, NULL, &tv);
		if (ret > 0) {
			ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, NULL, NULL);
			if (n > 0) {
				buf[n] = '\0';
				if (strcmp(buf, expected) == 0) {
					close(sock);
					return 0;
				}
			}
		}
	}

	close(sock);
	return -1;
}

static void write_rsync_config(void) {
	FILE* fp = fopen(RSYNC_CONF_PATH, "w");
	if (!fp)
		return;

	fprintf(fp, "pid file = %s\n", RSYNC_PID_PATH);
	fprintf(fp, "port = %d\n", SYNC_RSYNC_PORT);
	fprintf(fp, "use chroot = no\n");
	fprintf(fp, "read only = no\n");
	fprintf(fp, "uid = 0\n");
	fprintf(fp, "gid = 0\n");
	fprintf(fp, "log file = /tmp/rsyncd.log\n");
	fprintf(fp, "transfer logging = yes\n");
	fprintf(fp, "log format = %%o %%f (%%l bytes)\n");
	// Writable as root and unauthenticated: only the paired peer may connect.
	fprintf(fp, "hosts allow = %s\n\n", peer_ip);
	fprintf(fp, "[shared]\n");
	fprintf(fp, "  path = %s\n", SHARED_DATA_PATH);
	fprintf(fp, "  read only = no\n");
	// Only for a peer that filters too: the daemon refuses excluded files, and
	// old firmware fails its whole sync on that (rsync exit 23).
	char rules[1024];
	if (peer_filters && sync_shared_daemon_filter(rules, sizeof(rules)) > 0)
		fprintf(fp, "  filter = %s\n", rules);
	fprintf(fp, "\n");
	fprintf(fp, "[saves]\n");
	fprintf(fp, "  path = %s\n", saves_path());
	fprintf(fp, "  read only = no\n");
	if (peer_filters)
		fprintf(fp, "  exclude = %s\n", SYNC_JUNK_DAEMON_EXCLUDE);
	fprintf(fp, "\n");
	fprintf(fp, "[roms]\n");
	fprintf(fp, "  path = %s\n", ROMS_PATH);
	fprintf(fp, "  read only = no\n");
	if (peer_filters)
		fprintf(fp, "  exclude = %s\n", SYNC_JUNK_DAEMON_EXCLUDE);

	fclose(fp);
}

static int start_rsync_daemon(void) {
	if (access(rsync_bin_path(), X_OK) != 0)
		return -1;

	write_rsync_config();

	char cmd[MAX_PATH * 4 + 128];
	char bin_q[MAX_PATH * 4];
	snprintf(cmd, sizeof(cmd), "'%s' --daemon --config=%s", shq(rsync_bin_path(), bin_q, sizeof(bin_q)), RSYNC_CONF_PATH);
	int ret = system(cmd);
	if (ret != 0)
		return -1;

	usleep(500000);
	if (access(RSYNC_PID_PATH, F_OK) != 0)
		return -1;

	return 0;
}

static void stop_rsync_daemon(void) {
	FILE* fp = fopen(RSYNC_PID_PATH, "r");
	if (fp) {
		int pid;
		if (fscanf(fp, "%d", &pid) == 1 && pid > 0) {
			kill(pid, SIGTERM);
		}
		fclose(fp);
		unlink(RSYNC_PID_PATH);
	}
	system("killall rsync 2>/dev/null");
	unlink(RSYNC_CONF_PATH);
}

// Progress tracking for current phase
static volatile int phase_files_done = 0;
static volatile int phase_files_total = 0;
static volatile int phase_files_transferred = 0;

// Parse "xfr#N, to-chk=X/Y" from rsync --info=progress2 output.
static void parse_progress2(const char* line) {
	// Parse xfr#N (actual files transferred)
	const char* x = strstr(line, "xfr#");
	if (x) {
		int n = 0;
		if (sscanf(x + 4, "%d", &n) == 1)
			phase_files_transferred = n;
	}

	// Parse to-chk=X/Y (remaining/total scanned)
	const char* p = strstr(line, "to-chk=");
	if (!p)
		p = strstr(line, "to-check=");
	if (!p)
		return;

	p = strchr(p, '=');
	if (!p)
		return;
	p++;

	int remaining = 0, total = 0;
	if (sscanf(p, "%d/%d", &remaining, &total) == 2 && total > 0) {
		phase_files_total = total;
		phase_files_done = total - remaining;
	}
}

// Read a line delimited by \r or \n from a FILE stream.
// rsync --info=progress2 uses \r to update progress in-place.
static char* read_line_cr(char* buf, int size, FILE* fp) {
	int i = 0;
	int c;
	while (i < size - 1) {
		c = fgetc(fp);
		if (c == EOF) {
			if (i == 0)
				return NULL;
			break;
		}
		if (c == '\r' || c == '\n') {
			if (i > 0)
				break;
			continue; // skip leading \r\n
		}
		buf[i++] = (char)c;
	}
	buf[i] = '\0';
	return buf;
}

// Run rsync via popen() and capture output line by line into the log buffer.
// extra: options the ROM phases add (art in or out), "" for the others.
static int run_rsync_phase(int phase, const char* extra) {
	char cmd[MAX_PATH * 8 + 768]; // two quoted runtime paths + rsync options
	// --modify-window=1: FAT32 cards keep mtimes at 2-second resolution, so a
	// received file's timestamp rounds down and would otherwise count as
	// changed (and be re-sent) on every later run.
	// No --inplace: a cut transfer would leave a truncated file with a fresh
	// mtime, which --update then prefers over the good copy on both devices.
	// The receiver writes a temp file beside it and renames it when complete.
	const char* rsync_opts = "-rtv --update --no-perms --omit-dir-times --modify-window=1 --info=progress2 " SYNC_JUNK_CLIENT_ARGS;
	// Of .userdata/shared only the save states and their slot pictures and
	// markers travel (sync_proto.c); settings, recents and RA data stay put.
	char shared_filter[1024];
	if (sync_shared_client_args(shared_filter, sizeof(shared_filter)) < 0)
		return -1;

	phase_files_done = 0;
	phase_files_total = 0;
	phase_files_transferred = 0;

	char bin_q[MAX_PATH * 4];
	char dir_q[MAX_PATH * 4];
	switch (phase) {
	case 1:
		snprintf(cmd, sizeof(cmd), "'%s' %s %s '%s'/ rsync://%s:%d/shared/ 2>&1",
				 shq(rsync_bin_path(), bin_q, sizeof(bin_q)), rsync_opts, shared_filter, shq(SHARED_DATA_PATH, dir_q, sizeof(dir_q)), peer_ip, SYNC_RSYNC_PORT);
		break;
	case 2:
		snprintf(cmd, sizeof(cmd), "'%s' %s '%s'/ rsync://%s:%d/saves/ 2>&1",
				 shq(rsync_bin_path(), bin_q, sizeof(bin_q)), rsync_opts, shq(saves_path(), dir_q, sizeof(dir_q)), peer_ip, SYNC_RSYNC_PORT);
		break;
	case 3:
		snprintf(cmd, sizeof(cmd), "'%s' %s %s rsync://%s:%d/shared/ '%s'/ 2>&1",
				 shq(rsync_bin_path(), bin_q, sizeof(bin_q)), rsync_opts, shared_filter, peer_ip, SYNC_RSYNC_PORT, shq(SHARED_DATA_PATH, dir_q, sizeof(dir_q)));
		break;
	case 4:
		snprintf(cmd, sizeof(cmd), "'%s' %s rsync://%s:%d/saves/ '%s'/ 2>&1",
				 shq(rsync_bin_path(), bin_q, sizeof(bin_q)), rsync_opts, peer_ip, SYNC_RSYNC_PORT, shq(saves_path(), dir_q, sizeof(dir_q)));
		break;
	case 5:
		snprintf(cmd, sizeof(cmd), "'%s' %s %s '%s'/ rsync://%s:%d/roms/ 2>&1",
				 shq(rsync_bin_path(), bin_q, sizeof(bin_q)), rsync_opts, extra, shq(ROMS_PATH, dir_q, sizeof(dir_q)), peer_ip, SYNC_RSYNC_PORT);
		break;
	case 6:
		snprintf(cmd, sizeof(cmd), "'%s' %s %s rsync://%s:%d/roms/ '%s'/ 2>&1",
				 shq(rsync_bin_path(), bin_q, sizeof(bin_q)), rsync_opts, extra, peer_ip, SYNC_RSYNC_PORT, shq(ROMS_PATH, dir_q, sizeof(dir_q)));
		break;
	default:
		return -1;
	}

	FILE* fp = popen(cmd, "r");
	if (!fp)
		return -1;

	char line[512];
	while (read_line_cr(line, sizeof(line), fp) != NULL) {
		if (sync_cancel)
			break;

		// Skip blank lines and rsync noise
		if (line[0] == '\0')
			continue;
		if (strncmp(line, "sending incremental", 19) == 0)
			continue;
		if (strncmp(line, "receiving incremental", 20) == 0)
			continue;
		if (strncmp(line, "sent ", 5) == 0)
			continue;
		if (strncmp(line, "total size", 10) == 0)
			continue;

		// Check for progress2 info (contains "to-chk=" or "xfr#")
		if (strstr(line, "to-chk=") || strstr(line, "to-check=")) {
			parse_progress2(line);
			continue; // don't add raw progress lines to log
		}

		// Skip lines that are just progress percentages (e.g. "  1,234,567  50%  ...")
		if (line[0] == ' ' && strstr(line, "%"))
			continue;

		log_add(line);
	}

	int status = pclose(fp);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void* sync_thread_func(void* arg) {
	(void)arg;

	mkdir_p(SHARED_DATA_PATH);
	mkdir_p(saves_path());
	if (sync_roms)
		mkdir_p(ROMS_PATH);

	if (is_server) {
		log_add("Starting as server...");

		// Truncate daemon log before starting daemon so we read a clean file
		FILE* trunc = fopen("/tmp/rsyncd.log", "w");
		if (trunc)
			fclose(trunc);

		if (start_rsync_daemon() != 0) {
			log_add("ERROR: Failed to start rsync daemon");
			sync_result = -1;
			sync_done = true;
			return NULL;
		}

		log_add("rsync daemon started, waiting for client...");
		sync_phase = 0; // 0 = no phase yet; UI shows "Waiting for client..."

		int notify_sock = create_send_socket();
		int done_sock = create_recv_socket();
		if (done_sock < 0) {
			if (notify_sock >= 0)
				close(notify_sock);
			stop_rsync_daemon();
			log_add("ERROR: Failed to create socket");
			sync_result = -1;
			sync_done = true;
			return NULL;
		}

		char buf[256];
		time_t start = time(NULL);
		int ret = -1;
		uint32_t last_ready = 0;

		FILE* daemon_log = fopen("/tmp/rsyncd.log", "r");

		while (!sync_cancel && !app_quit) {
			if (time(NULL) - start > 3600) {
				log_add("ERROR: Timeout waiting for client");
				break;
			}

			uint32_t now = SDL_GetTicks();
			if (notify_sock >= 0 && now - last_ready >= 1000) {
				send_udp_message(notify_sock, READY_MSG, peer_ip);
				last_ready = now;
			}

			// Tail the rsync daemon log for new lines
			if (!daemon_log)
				daemon_log = fopen("/tmp/rsyncd.log", "r");
			if (daemon_log) {
				char logline[512];
				while (fgets(logline, sizeof(logline), daemon_log) != NULL) {
					// Strip trailing newline
					int ll = strlen(logline);
					if (ll > 0 && logline[ll - 1] == '\n')
						logline[ll - 1] = '\0';
					if (!logline[0])
						continue;

					// Strip "YYYY/MM/DD HH:MM:SS [PID] " prefix
					const char* msg = logline;
					const char* bracket = strchr(msg, ']');
					if (bracket && *(bracket + 1) == ' ')
						msg = bracket + 2;

					// For transfer lines ("send X" / "recv X"), show just the path
					if (strncmp(msg, "send ", 5) == 0)
						msg += 5;
					else if (strncmp(msg, "recv ", 5) == 0)
						msg += 5;

					if (msg[0])
						log_add(msg);
				}
				// fgets returns NULL at EOF; file handle stays valid for subsequent reads
			}

			fd_set readfds;
			FD_ZERO(&readfds);
			FD_SET(done_sock, &readfds);
			struct timeval tv = {0, 200000};

			int sel = select(done_sock + 1, &readfds, NULL, NULL, &tv);
			if (sel > 0) {
				ssize_t n = recvfrom(done_sock, buf, sizeof(buf) - 1, 0, NULL, NULL);
				if (n > 0) {
					buf[n] = '\0';
					if (strcmp(buf, DONE_MSG) == 0) {
						log_add("Client finished sync");
						ret = 0;
						break;
					}
				}
			}
		}

		if (daemon_log)
			fclose(daemon_log);
		if (notify_sock >= 0)
			close(notify_sock);
		close(done_sock);
		stop_rsync_daemon();

		sync_result = (ret == 0) ? 0 : -1;
		sync_done = true;
	} else {
		log_add("Starting as client...");
		log_add("Waiting for server...");
		sync_phase = 0;

		int ret = wait_for_udp_message(READY_MSG, 30);
		if (ret != 0) {
			log_add("ERROR: Server not ready (timeout)");
			sync_result = -1;
			sync_done = true;
			return NULL;
		}

		log_add("Server ready!");
		usleep(500000);

		int total_phases = sync_roms ? 6 : 4;
		const char* phase_names_4[] = {
			"",
			"[1/4] Pushing states",
			"[2/4] Pushing saves",
			"[3/4] Pulling states",
			"[4/4] Pulling saves",
		};
		const char* phase_names_6[] = {
			"",
			"[1/6] Pushing states",
			"[2/6] Pushing saves",
			"[3/6] Pulling states",
			"[4/6] Pulling saves",
			"[5/6] Pushing ROMs",
			"[6/6] Pulling ROMs",
		};
		const char** phase_names = sync_roms ? phase_names_6 : phase_names_4;

		int total_transferred = 0;
		int total_scanned = 0;
		for (int phase = 1; phase <= total_phases && !sync_cancel; phase++) {
			sync_phase = phase;
			log_add(phase_names[phase]);
			bool roms_phase = phase >= 5;
			int result = run_rsync_phase(phase, roms_phase ? SYNC_ROMS_NO_ART_ARGS : "");
			int phase_transferred = phase_files_transferred;
			int phase_scanned = phase_files_total;
			// Then the art this side or the peer has none of (never replaced).
			if (result == 0 && roms_phase && !sync_cancel) {
				log_add(phase == 5 ? "  Pushing missing art" : "  Pulling missing art");
				result = run_rsync_phase(phase, SYNC_ROMS_ART_ARGS);
				phase_transferred += phase_files_transferred;
				phase_scanned += phase_files_total;
			}
			if (result != 0) {
				char err[LOG_LINE_LEN];
				snprintf(err, sizeof(err), "ERROR: Phase %d failed (exit %d)", phase, result);
				log_add(err);
				sync_result = -1;
				sync_done = true;
				return NULL;
			}
			total_transferred += phase_transferred;
			total_scanned += phase_scanned;

			char summary[LOG_LINE_LEN];
			snprintf(summary, sizeof(summary), "  Done: %d files changed out of %d",
					 phase_transferred, phase_scanned);
			log_add(summary);
		}

		{
			char final_summary[LOG_LINE_LEN];
			snprintf(final_summary, sizeof(final_summary),
					 "Sync complete: %d files changed out of %d total",
					 total_transferred, total_scanned);
			log_add(final_summary);
		}

		int notify_sock = create_send_socket();
		if (notify_sock >= 0) {
			for (int i = 0; i < 5; i++) {
				send_udp_message(notify_sock, DONE_MSG, peer_ip);
				usleep(200000);
			}
			close(notify_sock);
		}

		sync_result = 0;
		sync_done = true;
	}

	return NULL;
}

static void start_sync(void) {
	// A handheld is never client-only; a phone is (its ROMs are not plain
	// files a daemon could serve).
	is_server = sync_is_server(false, peer_client_only, own_ip, peer_ip);

	// Either device enabling ROMS sync activates it for both
	if (peer_sync_roms)
		sync_roms = true;

	sync_cancel = false;
	sync_done = false;
	sync_result = 0;
	sync_phase = 0;

	log_clear();
	char msg[LOG_LINE_LEN];
	snprintf(msg, sizeof(msg), "Syncing with %s via %s", peer_ip, via_cable ? "cable" : "Wi-Fi");
	log_add(msg);
	snprintf(msg, sizeof(msg), "Role: %s", is_server ? "Server" : "Client");
	log_add(msg);
	if (sync_roms)
		log_add("ROMs sync: enabled");

	// Sleep already disabled from start_discovery(), ensure it stays disabled
	PWR_disableSleep();
	PWR_disableAutosleep();

	state = STATE_SYNCING;
	if (pthread_create(&sync_thread, NULL, sync_thread_func, NULL) != 0) {
		PWR_enableSleep();
		state = STATE_ERROR;
		return;
	}
	sync_thread_active = true;
}

static void cancel_sync(void) {
	sync_cancel = true;
	stop_rsync_daemon(); // kills daemon + killall rsync, unblocks popen in sync thread
	if (sync_thread_active) {
		pthread_join(sync_thread, NULL);
		sync_thread_active = false;
	}
	PWR_enableSleep();
	PWR_enableAutosleep();
}

static void render_log(int top_y, int bottom_y) {
	pthread_mutex_lock(&log_mutex);

	int line_h = SCALE1(FONT_SMALL + 2);
	int pad = SCALE1(PADDING * 2);
	int x = pad;
	int max_w = screen->w - pad * 2;
	int avail_h = bottom_y - top_y;
	int max_visible = avail_h / line_h;

	// Clip to padded area so long lines are truncated
	SDL_Rect clip = {x, top_y, max_w, avail_h};
	SDL_SetClipRect(screen, &clip);

	// Show the most recent lines that fit
	int start_idx = 0;
	if (log_count > max_visible)
		start_idx = log_count - max_visible;

	int y = top_y;
	for (int i = start_idx; i < log_count; i++) {
		GFX_blitText(font.small, log_lines[i], 0, COLOR_WHITE, screen,
					 &(SDL_Rect){x, y, max_w, line_h});
		y += line_h;
	}

	SDL_SetClipRect(screen, NULL);

	pthread_mutex_unlock(&log_mutex);
}

static void render_screen(void) {
	GFX_clear(screen);
	int menu_h = UI_renderMenuBar(screen, "Device Sync");

	switch (state) {
	case STATE_NO_WIFI:
		UI_renderCenteredMessage(screen, "Wi-Fi not connected.\nTurn on Wi-Fi, or sync via cable.");
		UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", NULL});
		break;

	case STATE_READY: {
		UI_renderButtonHintBar(screen, (char*[]){"B", "EXIT", "X", sync_roms ? "ROMS: ON" : "ROMS: OFF", "A", "START", NULL});
		int max_w = screen->w - SCALE1(PADDING * 2);
		int ll = TTF_FontLineSkip(font.large);
		int sl = TTF_FontLineSkip(font.small);
		int gap = SCALE1(PADDING * 2);
		int pill_h = SCALE1(PILL_SIZE);

		// Calculate total content height
		int total_h = ll * 2	   // title (2 lines)
					  + gap		   // gap
					  + pill_h * 2 // the two link options
					  + gap		   // gap
					  + sl * 2;	   // instructions (2 lines)

		// Center vertically on the full screen
		int y = (screen->h - total_h) / 2;

		GFX_blitText(font.large, sync_roms ? "Sync saves, states and ROMs\nbetween two devices." : "Sync saves and states\nbetween two devices.",
					 ll,
					 COLOR_WHITE, screen,
					 &(SDL_Rect){SCALE1(PADDING), y, max_w, ll * 2});
		y += ll * 2 + gap;


		// The two links, one pill each, the picked one highlighted.
		const char* options[] = {"Sync via cable", "Sync via Wi-Fi"};
		for (int i = 0; i < 2; i++) {
			bool selected = (i == 0) == via_cable;
			int tw = 0;
			TTF_SizeUTF8(font.large, options[i], &tw, NULL);
			int pw = tw + SCALE1(BUTTON_PADDING * 2);
			SDL_Rect pill = {(screen->w - pw) / 2, y, pw, pill_h};
			UI_drawListItemBg(screen, &pill, selected);
			GFX_blitText(font.large, options[i], 0, UI_getListTextColor(selected), screen,
						 &(SDL_Rect){pill.x, y + (pill_h - TTF_FontHeight(font.large)) / 2, pw, pill_h});
			y += pill_h;
		}
		y += gap;

		GFX_blitText(font.small, via_cable ? "Connect one device's top port to the\nother's bottom port, then pick cable on both." : "Put both devices on the same Wi-Fi,\nthen pick Wi-Fi on both.",
					 sl, COLOR_WHITE, screen,
					 &(SDL_Rect){SCALE1(PADDING), y, max_w, sl * 2});
	} break;

	case STATE_WAITING: {
		uint32_t now = SDL_GetTicks();
		if (now - last_dot_time > 500) {
			dot_count = (dot_count + 1) % 4;
			last_dot_time = now;
		}
		int large_leading = TTF_FontLineSkip(font.large);
		char msg[160];
		const char* link_error = via_cable ? cable_link_error() : NULL;
		if (link_error)
			snprintf(msg, sizeof(msg), "%s", link_error);
		else if (via_cable && !own_ip[0])
			snprintf(msg, sizeof(msg), "Waiting for the cable%.*s\n\nConnect one device's top port\nto the other's bottom port.",
					 dot_count + 1, "...");
		else
			snprintf(msg, sizeof(msg), "Searching for device%.*s\n\nOpen Sync on the other device.\nIP: %s (%s)",
					 dot_count + 1, "...", own_ip, via_cable ? "cable" : "Wi-Fi");

		int y = screen->h / 2 - large_leading * 2;
		GFX_blitText(font.large, msg, large_leading, COLOR_WHITE, screen,
					 &(SDL_Rect){SCALE1(PADDING), y, screen->w - SCALE1(PADDING * 2), screen->h});
	}
		UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", NULL});
		break;

	case STATE_SYNCING: {
		UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", NULL});

		int top_y = menu_h + SCALE1(PADDING);
		int bottom_y = screen->h - SCALE1(PILL_SIZE + PADDING);

		// Show phase progress header at top
		{
			int total_phases = sync_roms ? 6 : 4;
			const char* phase_labels[] = {
				"",
				"Pushing states",
				"Pushing saves",
				"Pulling states",
				"Pulling saves",
				"Pushing ROMs",
				"Pulling ROMs",
			};
			int p = sync_phase;
			if (p >= 1 && p <= total_phases) {
				char hdr[128];
				int done = phase_files_done;
				int total = phase_files_total;
				int xfr = phase_files_transferred;
				if (total > 0)
					snprintf(hdr, sizeof(hdr), "[%d/%d] %s - %d/%d files (%d changed)",
							 p, total_phases, phase_labels[p], done, total, xfr);
				else
					snprintf(hdr, sizeof(hdr), "[%d/%d] %s",
							 p, total_phases, phase_labels[p]);
				GFX_blitText(font.large, hdr, 0, COLOR_WHITE, screen,
							 &(SDL_Rect){SCALE1(PADDING), top_y,
										 screen->w - SCALE1(PADDING * 2), SCALE1(FONT_LARGE)});
			} else if (is_server) {
				GFX_blitText(font.large, "Waiting for client...", 0, COLOR_WHITE, screen,
							 &(SDL_Rect){SCALE1(PADDING), top_y,
										 screen->w - SCALE1(PADDING * 2), SCALE1(FONT_LARGE)});
			}
			top_y += TTF_FontLineSkip(font.large) + SCALE1(PADDING);
		}

		render_log(top_y, bottom_y);
		break;
	}

	case STATE_ERROR: {
		int top_y = menu_h + SCALE1(PADDING);
		int bottom_y = screen->h - SCALE1(PILL_SIZE + PADDING * 3 + FONT_LARGE);

		render_log(top_y, bottom_y);

		// Error message below log
		GFX_blitText(font.large, "Sync failed. Press A to retry.", 0, COLOR_WHITE, screen,
					 &(SDL_Rect){SCALE1(PADDING), bottom_y + SCALE1(PADDING),
								 screen->w - SCALE1(PADDING * 2), SCALE1(FONT_LARGE)});
	}
		UI_renderButtonHintBar(screen, (char*[]){"B", "EXIT", "A", "RETRY", NULL});
		break;

	case STATE_DONE: {
		int top_y = menu_h + SCALE1(PADDING);
		int bottom_y = screen->h - SCALE1(PILL_SIZE + PADDING * 3 + FONT_LARGE);

		render_log(top_y, bottom_y);

		GFX_blitText(font.large, "Sync complete!", 0, COLOR_WHITE, screen,
					 &(SDL_Rect){SCALE1(PADDING), bottom_y + SCALE1(PADDING),
								 screen->w - SCALE1(PADDING * 2), SCALE1(FONT_LARGE)});
	}
		UI_renderButtonHintBar(screen, (char*[]){"B", "EXIT", "A", "SYNC AGAIN", NULL});
		break;
	}

	GFX_flip(screen);
}

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;
	screen = GFX_init(MODE_MAIN);
	UI_showSplashScreen(screen, "Device Sync");

	PWR_pinToCores(CPU_CORE_EFFICIENCY);
	InitSettings();
	PAD_init();
	PWR_init();
	setup_signal_handlers();

	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!app_quit) {
		GFX_startFrame();
		PAD_poll();
		// MENU + SELECT exits (same combo as in-game and the other pak tools).
		bool req_quit = false;
		UI_handleQuitRequest(screen, &req_quit, &dirty, "Exit Device Sync?", NULL);
		if (req_quit)
			app_quit = true;
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (UI_statusBarChanged())
			dirty = true;

		switch (state) {
		case STATE_NO_WIFI:
			if (PAD_justPressed(BTN_B)) {
				state = STATE_READY;
				dirty = true;
			}
			break;

		case STATE_READY:
			if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_DOWN)) {
				via_cable = !via_cable;
				dirty = true;
			}
			if (PAD_justPressed(BTN_A)) {
				if (via_cable) {
					usblink_owned = access(USBLINK_PID_PATH, F_OK) != 0;
					start_discovery();
				} else if (wifi_ready()) {
					start_discovery();
				} else {
					state = STATE_NO_WIFI;
				}
				dirty = true;
			}
			if (PAD_justPressed(BTN_X)) {
				sync_roms = !sync_roms;
				dirty = true;
			}
			if (PAD_justPressed(BTN_B))
				app_quit = true;
			break;

		case STATE_WAITING:
			dirty = true;
			if (PAD_justPressed(BTN_B)) {
				stop_discovery();
				cable_link_stop();
				PWR_enableSleep();
				PWR_enableAutosleep();
				state = STATE_READY;
			} else if (peer_found) {
				__sync_synchronize();
				stop_discovery();
				start_sync();
				dirty = true;
			}
			break;

		case STATE_SYNCING:
			dirty = true;
			if (PAD_justPressed(BTN_B)) {
				cancel_sync();
				cable_link_stop();
				state = STATE_ERROR;
				dirty = true;
			} else if (sync_done) {
				if (sync_thread_active) {
					pthread_join(sync_thread, NULL);
					sync_thread_active = false;
				}
				stop_rsync_daemon();
				// The session is over; Sync again starts a new one.
				cable_link_stop();
				PWR_enableSleep();
				PWR_enableAutosleep();

				if (sync_roms) {
					unlink(EMULIST_CACHE_PATH);
					unlink(ROMINDEX_CACHE_PATH);
				}

				if (sync_result == 0) {
					state = STATE_DONE;
				} else {
					state = STATE_ERROR;
				}
				dirty = true;
			}
			break;

		case STATE_ERROR:
			if (PAD_justPressed(BTN_A)) {
				log_clear();
				state = STATE_READY;
				dirty = true;
			}
			if (PAD_justPressed(BTN_B))
				app_quit = true;
			break;

		case STATE_DONE:
			if (PAD_justPressed(BTN_A)) {
				log_clear();
				state = STATE_READY;
				dirty = true;
			}
			if (PAD_justPressed(BTN_B))
				app_quit = true;
			break;
		}

		if (dirty) {
			render_screen();
			dirty = false;
		} else {
			GFX_sync();
		}
	}

	if (discovery_thread_active) {
		discovery_running = false;
		pthread_join(discovery_thread, NULL);
	}
	if (sync_thread_active) {
		sync_cancel = true;
		pthread_join(sync_thread, NULL);
	}
	stop_rsync_daemon();
	cable_link_stop();

	if (udp_sock >= 0)
		close(udp_sock);

	PWR_enableSleep();
	PWR_enableAutosleep();

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();

	return EXIT_SUCCESS;
}
