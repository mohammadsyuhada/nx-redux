// Host test for netplay/core_netplay.c: the state minarch keeps for a netplay
// session the core runs itself (flycast's GGPO), and the leave dialog's
// countdown.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "../../netplay/core_netplay.h"

int main(void) {
	// off unless the launcher asked for it
	unsetenv("NX_CORE_NETPLAY");
	CoreNetplay_initFromEnv();
	assert(!CoreNetplay_isActive());
	assert(!CoreNetplay_hasEnded());

	setenv("NX_CORE_NETPLAY", "0", 1);
	CoreNetplay_initFromEnv();
	assert(!CoreNetplay_isActive());

	setenv("NX_CORE_NETPLAY", "1", 1);
	CoreNetplay_initFromEnv();
	assert(CoreNetplay_isActive());
	assert(!CoreNetplay_hasEnded());

	// the core ending the session (peer left) is remembered for the message,
	// and the session counts as over from then on
	CoreNetplay_markEnded();
	assert(CoreNetplay_hasEnded());
	assert(!CoreNetplay_isActive());

	// a fresh start clears it
	CoreNetplay_initFromEnv();
	assert(CoreNetplay_isActive());
	assert(!CoreNetplay_hasEnded());

	// the leave dialog shorter than the core's disconnect timeout, so the
	// player's own choice (or the dialog's timeout) always comes first
	assert(CORE_NETPLAY_LEAVE_GRACE_MS == 20000);
	assert(CORE_NETPLAY_DISCONNECT_MS > CORE_NETPLAY_LEAVE_GRACE_MS);

	// countdown: whole seconds left, rounded up, never negative
	assert(CoreNetplay_leaveSecondsLeft(1000, 1000) == 20);
	assert(CoreNetplay_leaveSecondsLeft(1000, 1001) == 20);
	assert(CoreNetplay_leaveSecondsLeft(1000, 2000) == 19);
	assert(CoreNetplay_leaveSecondsLeft(1000, 20999) == 1);
	assert(CoreNetplay_leaveSecondsLeft(1000, 21000) == 0);
	assert(CoreNetplay_leaveSecondsLeft(1000, 99000) == 0);
	// SDL tick wraparound
	assert(CoreNetplay_leaveSecondsLeft(0xFFFFFF00u, 0x00000100u) == 20);

	// Leaving says goodbye so the other side ends at once instead of after the
	// core's disconnect timeout. Over loopback the peer is ourselves.
	const uint16_t port = 55942; // test-only, clear of the real CORE_NETPLAY_BYE_PORT
	setenv("NX_CORE_NETPLAY", "1", 1);
	setenv("NX_GGPO_SERVER", "127.0.0.1", 1);
	CoreNetplay_initFromEnv();
	assert(CoreNetplay_byeOpen(port) == 0);
	assert(CoreNetplay_byePoll() == 0); // nothing yet
	CoreNetplay_byeSend(port);
	usleep(50000);
	assert(CoreNetplay_byePoll() == 1);
	assert(CoreNetplay_byePoll() == 0); // drained (the repeats count once)

	// only the peer's goodbye counts: another payload, or the right payload
	// from an address that isn't the peer, is ignored
	int raw = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in to = {0};
	to.sin_family = AF_INET;
	to.sin_port = htons(port);
	to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sendto(raw, "HELLO", 5, 0, (struct sockaddr*)&to, sizeof(to));
	usleep(50000);
	assert(CoreNetplay_byePoll() == 0);
	CoreNetplay_byeClose();

	setenv("NX_GGPO_SERVER", "10.9.9.9", 1);
	CoreNetplay_initFromEnv();
	assert(CoreNetplay_byeOpen(port) == 0);
	sendto(raw, CORE_NETPLAY_BYE_MSG, strlen(CORE_NETPLAY_BYE_MSG), 0, (struct sockaddr*)&to, sizeof(to));
	usleep(50000);
	assert(CoreNetplay_byePoll() == 0);
	CoreNetplay_byeClose();
	close(raw);

	// no session: nothing opens, polling is a no-op
	unsetenv("NX_CORE_NETPLAY");
	CoreNetplay_initFromEnv();
	assert(CoreNetplay_byeOpen(port) == -1);
	assert(CoreNetplay_byePoll() == 0);
	CoreNetplay_byeSend(port); // harmless
	CoreNetplay_byeClose();

	printf("test_core_netplay: OK\n");
	return 0;
}
