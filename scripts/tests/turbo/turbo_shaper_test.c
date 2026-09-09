// Host unit test for minarch's turbo shaper (workspace/all/minarch/ma_turbo.c).
//
// Models the TrimUI stock input daemon (trimui_inputd), which implements turbo
// by flipping a phase bit on every GPIO poll and reporting turbo-flagged
// buttons as released on the odd polls: the Brick daemon polls every 8333us
// (60 Hz square wave, 8 ms pulses), the vendored Smart Pro S one every
// 16000us (31 Hz, 16 ms pulses). minarch samples the pad once per 60 fps
// frame, so the raw signal aliases into "always held" or "never pressed".
// The shaper must reconstruct "physically held" from that pulse train and emit
// a clean, frame-safe cadence.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ma_turbo.h"

#define FRAME_US 16667
#define ID 1 // any button slot

static int failures = 0;
#define CHECK(cond, ...)                  \
	do {                                  \
		if (!(cond)) {                    \
			failures++;                   \
			printf("FAIL: " __VA_ARGS__); \
			printf("\n");                 \
		}                                 \
	} while (0)

// Simulate `frames` frames of a physically held button pulsed by a daemon
// with poll period `poll_us` (0 = no daemon pulsing, plain hold). Fills
// raw[] with what PAD_isPressed() would return at frame end and out[] with
// the shaped output.
static void simulate(TurboState* s, long poll_us, int frames, int* raw, int* out) {
	long long t_poll = 0; // next daemon poll time
	int phase = 0;		  // daemon phase bit
	int state = 0;		  // reported button state
	for (int f = 0; f < frames; f++) {
		long long end = (long long)(f + 1) * FRAME_US;
		int pressed_ev = 0, released_ev = 0;
		if (poll_us == 0) {
			if (!state) {
				state = 1;
				pressed_ev = 1;
			}
		} else {
			while (t_poll < end) {
				int next = phase ? 0 : 1; // odd polls: turbo buttons reported released
				if (next != state) {
					state = next;
					if (next)
						pressed_ev = 1;
					else
						released_ev = 1;
				}
				phase = !phase;
				t_poll += poll_us;
			}
		}
		raw[f] = state;
		int activity = state || pressed_ev || released_ev;
		out[f] = Turbo_step(s, ID, activity);
	}
}

static int rising_edges(const int* v, int n) {
	int prev = 0, edges = 0;
	for (int i = 0; i < n; i++) {
		if (v[i] && !prev)
			edges++;
		prev = v[i];
	}
	return edges;
}

// Every press must last exactly TURBO_ON_FRAMES and every gap TURBO_OFF_FRAMES
// (ignoring the tail of the window).
static int cadence_ok(const int* v, int n) {
	int i = 0;
	while (i < n && !v[i])
		i++;
	while (i + TURBO_ON_FRAMES + TURBO_OFF_FRAMES <= n) {
		for (int k = 0; k < TURBO_ON_FRAMES; k++)
			if (!v[i + k])
				return 0;
		for (int k = 0; k < TURBO_OFF_FRAMES; k++)
			if (v[i + TURBO_ON_FRAMES + k])
				return 0;
		i += TURBO_ON_FRAMES + TURBO_OFF_FRAMES;
	}
	return 1;
}

int main(void) {
	enum { N = 600 }; // 10 s at 60 fps
	static int raw[N], out[N];
	const int expect = N / (TURBO_ON_FRAMES + TURBO_OFF_FRAMES);
	TurboState s;

	// 1. The bug: the Brick daemon's 8 ms pulses sampled at 60 fps alias to a
	//    constant -- the core sees at most one press in 10 s of "turbo".
	Turbo_reset(&s);
	simulate(&s, 8333, N, raw, out);
	CHECK(rising_edges(raw, N) <= 1, "brick raw: expected aliased raw signal, got %d edges", rising_edges(raw, N));
	// ...and the shaped output is a clean cadence at the intended rate.
	CHECK(abs(rising_edges(out, N) - expect) <= 1, "brick shaped: %d presses, want ~%d", rising_edges(out, N), expect);
	CHECK(cadence_ok(out, N), "brick shaped: cadence broken");

	// 2. Same daemon with realistic loop overhead (period drifts past 8333us).
	Turbo_reset(&s);
	simulate(&s, 8400, N, raw, out);
	CHECK(abs(rising_edges(out, N) - expect) <= 1, "brick drift shaped: %d presses, want ~%d", rising_edges(out, N), expect);
	CHECK(cadence_ok(out, N), "brick drift shaped: cadence broken");

	// 3. Smart Pro S daemon (16 ms pulses).
	Turbo_reset(&s);
	simulate(&s, 16000, N, raw, out);
	CHECK(abs(rising_edges(out, N) - expect) <= 1, "sps shaped: %d presses, want ~%d", rising_edges(out, N), expect);
	CHECK(cadence_ok(out, N), "sps shaped: cadence broken");

	// 4. Plain hold (no daemon pulsing) shapes identically.
	Turbo_reset(&s);
	simulate(&s, 0, N, raw, out);
	CHECK(abs(rising_edges(out, N) - expect) <= 1, "plain hold: %d presses, want ~%d", rising_edges(out, N), expect);
	CHECK(out[0] == 1, "plain hold: first frame of a hold must press immediately");

	// 5. Release: once the pulses stop the output drops within the grace
	//    window and stays down.
	Turbo_reset(&s);
	simulate(&s, 8333, 30, raw, out);
	int after[60];
	for (int f = 0; f < 60; f++)
		after[f] = Turbo_step(&s, ID, 0);
	int last_press = -1;
	for (int f = 0; f < 60; f++)
		if (after[f])
			last_press = f;
	CHECK(last_press < TURBO_RELEASE_GRACE, "release: still pressed %d frames after the last event", last_press + 1);

	// 6. A single-frame event gap (frame pacing jitter) must not restart the
	//    cadence: the pattern continues as if uninterrupted.
	Turbo_reset(&s);
	int jit[16];
	for (int f = 0; f < 16; f++)
		jit[f] = Turbo_step(&s, ID, f == 5 ? 0 : 1);
	int ref[16];
	Turbo_reset(&s);
	for (int f = 0; f < 16; f++)
		ref[f] = Turbo_step(&s, ID, 1);
	CHECK(memcmp(jit, ref, sizeof(jit)) == 0, "jitter: one missed frame changed the cadence");

	// 7. Idle buttons stay released and independent of each other.
	Turbo_reset(&s);
	Turbo_step(&s, 3, 1);
	CHECK(Turbo_step(&s, ID, 0) == 0, "idle: untouched button reported pressed");

	if (failures) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	puts("turbo shaper: all checks passed");
	return 0;
}
