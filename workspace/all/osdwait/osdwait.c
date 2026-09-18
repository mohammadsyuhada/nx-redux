// libosdwait.so — LD_PRELOAD shim for the stock TrimUI OSD daemon (trimui_osdd).
//
// The daemon's input thread blocks in SDL_WaitEvent. It never creates an SDL
// window, so SDL 2.30's wait has nothing to block on and falls back to
// "pump, then SDL_Delay(1)": ~920 wakeups/s and ~4.5% of a core, forever,
// while the panel is hidden and nothing is happening (measured 2026-09-19 on
// Brick Pro and Smart Pro S — two thirds of the daemon's idle CPU). The daemon
// imports only SDL_WaitEvent, so a byte patch cannot switch it to a cheaper
// poll; interposing that one symbol can. MinUI.pak/launch.sh starts the daemon
// with LD_PRELOAD pointing here (see .dev/OSD.md, "Idle wait shim").
//
// Semantics preserved: returns 1 with the event filled in, exactly like
// SDL_WaitEvent; pending events come back immediately with no sleep between
// them, so a backlog still drains in one go (the hidden-state discard loop in
// the daemon relies on that — see the input drain patch). Only the *idle*
// cadence changes: 20 ms between polls while the panel is hidden, 1 ms while
// it is shown (SDL's own cadence, so visible input latency is unchanged).
// "Shown" is the daemon's own state marker file, one access() per idle tick.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>

#define STATE_PATH "/tmp/trimui_osd/osdd_show_up"
#define HIDDEN_POLL_US 20000
#define SHOWN_POLL_US 1000

typedef int (*wait_timeout_fn)(void* event, int timeout_ms);
typedef int (*wait_fn)(void* event);

int SDL_WaitEvent(void* event) {
	static wait_timeout_fn poll_fn;
	static wait_fn real_wait;
	static int resolved;
	if (!resolved) {
		poll_fn = (wait_timeout_fn)dlsym(RTLD_NEXT, "SDL_WaitEventTimeout");
		real_wait = (wait_fn)dlsym(RTLD_NEXT, "SDL_WaitEvent");
		resolved = 1;
	}
	if (!poll_fn) // unexpected SDL: behave exactly like the original
		return real_wait ? real_wait(event) : 0;
	for (;;) {
		if (poll_fn(event, 0)) // timeout 0: pump once, return a pending event
			return 1;
		usleep(access(STATE_PATH, F_OK) == 0 ? SHOWN_POLL_US : HIDDEN_POLL_US);
	}
}
