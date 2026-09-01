#include "desktop_probe.h"
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

int desktop_probe_reachable(const char* host, int port, int timeout_ms) {
	char portstr[16];
	snprintf(portstr, sizeof(portstr), "%d", port);
	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res)
		return 0;
	int ok = 0;
	for (struct addrinfo* ai = res; ai && !ok; ai = ai->ai_next) {
		int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;
		int flags = fcntl(fd, F_GETFL, 0);
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
		int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
		if (r == 0) {
			ok = 1;
		} else {
			fd_set wfds;
			FD_ZERO(&wfds);
			FD_SET(fd, &wfds);
			struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
			if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0) {
				int err = 0;
				socklen_t len = sizeof(err);
				getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
				ok = (err == 0);
			}
		}
		close(fd);
	}
	freeaddrinfo(res);
	return ok;
}
