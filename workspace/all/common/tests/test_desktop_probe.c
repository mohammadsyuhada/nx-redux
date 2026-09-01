// Host test for the desktop reachability probe. Starts a local listener and
// asserts the probe finds it, and that a closed port reads as unreachable.
#include <assert.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include "../desktop_probe.h"

static int listen_fd;
static int listen_port;
static void start_listener(void) {
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = {0};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = 0;
	bind(listen_fd, (struct sockaddr*)&a, sizeof(a));
	socklen_t l = sizeof(a);
	getsockname(listen_fd, (struct sockaddr*)&a, &l);
	listen_port = ntohs(a.sin_port);
	listen(listen_fd, 1);
}

int main(void) {
	start_listener();
	assert(desktop_probe_reachable("127.0.0.1", listen_port, 1000) == 1);
	close(listen_fd);
	// a port with nothing listening: unreachable (connection refused, fast)
	assert(desktop_probe_reachable("127.0.0.1", 1, 1000) == 0);
	printf("test_desktop_probe: OK\n");
	return 0;
}
