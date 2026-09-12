#include "music_service_client.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int64_t monotonic_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int wait_fd_until(int fd, short events, int64_t deadline_ms) {
	for (;;) {
		int64_t remaining = deadline_ms - monotonic_ms();
		if (remaining <= 0)
			return -1;
		int timeout = remaining > INT32_MAX ? INT32_MAX : (int)remaining;
		struct pollfd p = {.fd = fd, .events = events};
		int result = poll(&p, 1, timeout);
		if (result > 0) {
			if (p.revents & events)
				return 0;
			if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
				return -1;
			continue;
		}
		if (result == 0)
			return -1;
		if (errno != EINTR)
			return -1;
	}
}

static int transfer_until(int fd, void* data, size_t length, int writing, int64_t deadline_ms) {
	size_t done = 0;
	while (done < length) {
		if (wait_fd_until(fd, writing ? POLLOUT : POLLIN, deadline_ms) != 0)
			return -1;
		ssize_t result;
		if (writing)
			result = send(fd, (char*)data + done, length - done, MSG_NOSIGNAL | MSG_DONTWAIT);
		else
			result = recv(fd, (char*)data + done, length - done, MSG_DONTWAIT);
		if (result > 0) {
			done += (size_t)result;
			continue;
		}
		if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
			continue;
		return -1;
	}
	return 0;
}

const char* MusicService_socketPath(void) {
	const char* path = getenv(MUSIC_SERVICE_SOCKET_ENV);
	return path && path[0] ? path : MUSIC_SERVICE_DEFAULT_SOCKET;
}

int MusicService_connect(const char* socket_path, int timeout_ms) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	struct sockaddr_un address;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	if (!socket_path)
		socket_path = MusicService_socketPath();
	if (strlen(socket_path) >= sizeof(address.sun_path)) {
		close(fd);
		return -1;
	}
	strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
		close(fd);
		return -1;
	}
	int64_t deadline = monotonic_ms() + (timeout_ms > 0 ? timeout_ms : 1);
	int result = connect(fd, (struct sockaddr*)&address, sizeof(address));
	if (result != 0) {
		if (errno != EINPROGRESS || wait_fd_until(fd, POLLOUT, deadline) != 0) {
			close(fd);
			return -1;
		}
		int error = 0;
		socklen_t error_length = sizeof(error);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_length) != 0 || error != 0) {
			close(fd);
			return -1;
		}
	}
	return fd;
}

void MusicService_disconnect(int fd) {
	if (fd >= 0)
		close(fd);
}

int MusicService_request(int fd, uint16_t command, const void* payload,
						 size_t payload_length, MusicResponseWire* response,
						 int timeout_ms) {
	if (response)
		memset(response, 0, sizeof(*response));
	if (fd < 0 || !response || payload_length > MUSIC_SERVICE_MAX_FRAME - sizeof(MusicFrameHeader))
		return MUSIC_SERVICE_TRANSPORT_ERROR;
	MusicFrameHeader header = {
		.magic = MUSIC_SERVICE_MAGIC,
		.version = MUSIC_SERVICE_PROTOCOL_VERSION,
		.command = command,
		.request_id = 1,
		.payload_length = (uint32_t)payload_length};
	int64_t deadline = monotonic_ms() + (timeout_ms > 0 ? timeout_ms : 1);
	if (transfer_until(fd, &header, sizeof(header), 1, deadline) != 0 ||
		(payload_length && transfer_until(fd, (void*)payload, payload_length, 1, deadline) != 0))
		return MUSIC_SERVICE_TRANSPORT_ERROR;
	if (transfer_until(fd, &header, sizeof(header), 0, deadline) != 0 ||
		header.magic != MUSIC_SERVICE_MAGIC || header.version != MUSIC_SERVICE_PROTOCOL_VERSION ||
		header.payload_length != sizeof(*response) || header.payload_length > MUSIC_SERVICE_MAX_FRAME - sizeof(MusicFrameHeader) ||
		header.command != command || transfer_until(fd, response, sizeof(*response), 0, deadline) != 0)
		return MUSIC_SERVICE_TRANSPORT_ERROR;
	return response->status;
}
