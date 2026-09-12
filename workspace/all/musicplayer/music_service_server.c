#include "music_service_server.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 8
#define CLIENT_FRAME_TIMEOUT_MS 2000

typedef struct {
	int fd;
	unsigned char input[MUSIC_SERVICE_MAX_FRAME];
	size_t input_length;
	int64_t frame_deadline_ms;
} Client;

static int listen_fd = -1;
static char socket_path[sizeof(((struct sockaddr_un*)0)->sun_path)];
static char lock_path[sizeof(socket_path) + 8];
static Client clients[MAX_CLIENTS];

static int64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void close_client(Client* client) {
	if (client->fd >= 0)
		close(client->fd);
	client->fd = -1;
	client->input_length = 0;
	client->frame_deadline_ms = 0;
}

static int acquire_lock(void) {
	int fd = open(lock_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd >= 0) {
		char pid[32];
		int n = snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
		(void)write(fd, pid, (size_t)n);
		close(fd);
		return 0;
	}
	if (errno != EEXIST)
		return -1;
	fd = open(lock_path, O_RDONLY);
	char pid[32] = {0};
	ssize_t n = fd >= 0 ? read(fd, pid, sizeof(pid) - 1) : -1;
	if (fd >= 0)
		close(fd);
	if (n > 0) {
		pid_t owner = (pid_t)strtol(pid, NULL, 10);
		if (owner > 0 && kill(owner, 0) == 0)
			return -1;
	}
	unlink(lock_path);
	fd = open(lock_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0)
		return -1;
	char own_pid[32];
	n = snprintf(own_pid, sizeof(own_pid), "%ld\n", (long)getpid());
	(void)write(fd, own_pid, (size_t)n);
	close(fd);
	return 0;
}

int MusicServiceServer_open(void) {
	const char* configured = getenv(MUSIC_SERVICE_SOCKET_ENV);
	if (!configured || !configured[0])
		configured = MUSIC_SERVICE_DEFAULT_SOCKET;
	if (strlen(configured) >= sizeof(socket_path))
		return -1;
	strcpy(socket_path, configured);
	snprintf(lock_path, sizeof(lock_path), "%s.lock", socket_path);
	char parent[sizeof(socket_path)];
	strcpy(parent, socket_path);
	char* slash = strrchr(parent, '/');
	if (slash) {
		*slash = '\0';
		if (parent[0])
			mkdir(parent, 0755);
	}
	if (acquire_lock() != 0)
		return -1;
	listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listen_fd < 0)
		return -1;
	struct sockaddr_un address;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);
	if (bind(listen_fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
		if (errno == EADDRINUSE) {
			int probe = socket(AF_UNIX, SOCK_STREAM, 0);
			int alive = probe >= 0 && connect(probe, (struct sockaddr*)&address, sizeof(address)) == 0;
			if (probe >= 0)
				close(probe);
			if (alive) {
				unlink(lock_path);
				return -1;
			}
			unlink(socket_path);
			if (bind(listen_fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
				unlink(lock_path);
				return -1;
			}
		} else {
			unlink(lock_path);
			return -1;
		}
	}
	if (listen(listen_fd, MAX_CLIENTS) != 0)
		return -1;
	fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL, 0) | O_NONBLOCK);
	for (int i = 0; i < MAX_CLIENTS; i++)
		clients[i].fd = -1;
	return 0;
}

void MusicServiceServer_close(void) {
	for (int i = 0; i < MAX_CLIENTS; i++)
		close_client(&clients[i]);
	if (listen_fd >= 0)
		close(listen_fd);
	listen_fd = -1;
	if (socket_path[0])
		unlink(socket_path);
	if (lock_path[0])
		unlink(lock_path);
}

static void send_response(Client* client, uint16_t command, uint32_t request_id,
						  MusicResponseWire* response) {
	MusicFrameHeader header = {
		.magic = MUSIC_SERVICE_MAGIC, .version = MUSIC_SERVICE_PROTOCOL_VERSION, .command = command, .request_id = request_id, .payload_length = sizeof(*response)};
	unsigned char frame[sizeof(header) + sizeof(*response)];
	memcpy(frame, &header, sizeof(header));
	memcpy(frame + sizeof(header), response, sizeof(*response));
	size_t total = sizeof(frame), sent = 0;
	while (sent < total) {
		ssize_t n = send(client->fd, frame + sent, total - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
		if (n <= 0) {
			close_client(client);
			return;
		}
		sent += (size_t)n;
	}
}

static void process_client(Client* client, MusicServiceCommandHandler handler) {
	for (;;) {
		if (client->input_length < sizeof(MusicFrameHeader))
			return;
		MusicFrameHeader header;
		memcpy(&header, client->input, sizeof(header));
		if (header.magic != MUSIC_SERVICE_MAGIC || header.version != MUSIC_SERVICE_PROTOCOL_VERSION ||
			header.payload_length > MUSIC_SERVICE_MAX_FRAME - sizeof(MusicFrameHeader)) {
			close_client(client);
			return;
		}
		size_t frame_size = sizeof(header) + header.payload_length;
		if (client->input_length < frame_size)
			return;
		MusicResponseWire response;
		handler(header.command, client->input + sizeof(header), header.payload_length, &response);
		size_t remaining = client->input_length - frame_size;
		memmove(client->input, client->input + frame_size, remaining);
		client->input_length = remaining;
		client->frame_deadline_ms = remaining ? now_ms() + CLIENT_FRAME_TIMEOUT_MS : 0;
		send_response(client, header.command, header.request_id, &response);
		if (client->fd < 0)
			return;
	}
}

static void accept_clients(void) {
	for (;;) {
		int fd = accept(listen_fd, NULL, NULL);
		if (fd < 0)
			return;
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		int slot = -1;
		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (clients[i].fd < 0) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			close(fd);
			continue;
		}
		clients[slot].fd = fd;
		clients[slot].input_length = 0;
		clients[slot].frame_deadline_ms = 0;
	}
}

void MusicServiceServer_poll(MusicServiceCommandHandler handler, int timeout_ms) {
	struct pollfd pfds[1 + MAX_CLIENTS];
	int map[1 + MAX_CLIENTS];
	int count = 1;
	pfds[0] = (struct pollfd){.fd = listen_fd, .events = POLLIN};
	map[0] = -1;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (clients[i].fd >= 0) {
			pfds[count] = (struct pollfd){.fd = clients[i].fd, .events = POLLIN};
			map[count++] = i;
		}
	}
	(void)poll(pfds, count, timeout_ms);
	if (pfds[0].revents & POLLIN)
		accept_clients();
	int64_t now = now_ms();
	for (int p = 1; p < count; p++) {
		Client* client = &clients[map[p]];
		if (client->fd < 0)
			continue;
		if (pfds[p].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			close_client(client);
			continue;
		}
		if (pfds[p].revents & POLLIN) {
			unsigned char buf[512];
			ssize_t n = recv(client->fd, buf, sizeof(buf), MSG_DONTWAIT);
			if (n <= 0) {
				close_client(client);
				continue;
			}
			if (client->input_length + (size_t)n > MUSIC_SERVICE_MAX_FRAME) {
				close_client(client);
				continue;
			}
			if (client->input_length == 0)
				client->frame_deadline_ms = now + CLIENT_FRAME_TIMEOUT_MS;
			memcpy(client->input + client->input_length, buf, (size_t)n);
			client->input_length += (size_t)n;
			process_client(client, handler);
		}
		if (client->fd >= 0 && client->input_length && client->frame_deadline_ms > 0 &&
			now >= client->frame_deadline_ms)
			close_client(client);
	}
}
