// Host-compiled unit test for spk_guard.c (no device toolchain).
// Build & run:
//   cc -I. spk_guard.c tests/test_spk_guard.c -o /tmp/test_spk_guard && /tmp/test_spk_guard
#include <assert.h>
#include <stdio.h>
#include "../spk_guard.h"

#define DAC 34 // a representative user "DAC Volume" level

int main(void) {
	// --- initial: force the amp muted, don't touch the DAC ---
	{
		SpkGuard g;
		spk_guard_init(&g);
		assert(spk_guard_step(&g, 0, DAC, 0) == SPK_GUARD_AMP_OFF);
		assert(g.amp_on == 0);
		// Idle, codec off: nothing.
		assert(spk_guard_step(&g, 0, DAC, 50) == SPK_GUARD_NONE);
		assert(spk_guard_step(&g, 0, DAC, 5000) == SPK_GUARD_NONE);
	}

	// --- full cycle: DAC_MUTE -> AMP_ON@>=350 -> DAC_RESTORE@>=+150 -> AMP_OFF -
	{
		SpkGuard g;
		spk_guard_init(&g);
		assert(spk_guard_step(&g, 0, DAC, 0) == SPK_GUARD_AMP_OFF); // baseline
		// Stream opens, codec powers up (real volume): mute the DAC first.
		assert(spk_guard_step(&g, 1, DAC, 100) == SPK_GUARD_DAC_MUTE);
		// DAC now reads 0 (we muted it) — must not be mistaken for the user's 0.
		assert(spk_guard_step(&g, 1, 0, 100 + 349) == SPK_GUARD_NONE);
		// Settle reached: enable the amp into the muted DAC.
		assert(spk_guard_step(&g, 1, 0, 100 + 350) == SPK_GUARD_AMP_ON);
		assert(g.amp_on == 1);
		// Amp not yet settled: still muted.
		assert(spk_guard_step(&g, 1, 0, 100 + 350 + 149) == SPK_GUARD_NONE);
		// Amp settled: restore the saved volume in one write.
		assert(spk_guard_step(&g, 1, 0, 100 + 350 + 150) == SPK_GUARD_DAC_RESTORE);
		// Active: DAC now at the user level again, nothing to do.
		assert(spk_guard_step(&g, 1, DAC, 100 + 900) == SPK_GUARD_NONE);
		assert(spk_guard_step(&g, 1, DAC, 100 + 1500) == SPK_GUARD_NONE);
		// Stream closes: codec powers down -> force the amp off.
		assert(spk_guard_step(&g, 0, DAC, 100 + 1600) == SPK_GUARD_AMP_OFF);
		assert(g.amp_on == 0);
		assert(spk_guard_step(&g, 0, DAC, 100 + 1650) == SPK_GUARD_NONE);
	}

	// --- boot-prime blip (pm on 100 ms, off): DAC_MUTE then DAC_RESTORE only ---
	{
		SpkGuard g;
		spk_guard_init(&g);
		assert(spk_guard_step(&g, 0, DAC, 0) == SPK_GUARD_AMP_OFF);
		assert(spk_guard_step(&g, 1, DAC, 1000) == SPK_GUARD_DAC_MUTE);
		assert(spk_guard_step(&g, 1, 0, 1050) == SPK_GUARD_NONE); // 50 ms in
		// Powers down before the settle: restore the DAC, never enabled the amp.
		assert(spk_guard_step(&g, 0, 0, 1100) == SPK_GUARD_DAC_RESTORE);
		assert(g.amp_on == 0); // amp stayed off the whole time
		assert(spk_guard_step(&g, 0, DAC, 1150) == SPK_GUARD_NONE);
	}

	// --- power-down during AMP_ON_WAIT: AMP_OFF then DAC_RESTORE both emitted --
	{
		SpkGuard g;
		spk_guard_init(&g);
		assert(spk_guard_step(&g, 0, DAC, 0) == SPK_GUARD_AMP_OFF);
		assert(spk_guard_step(&g, 1, DAC, 100) == SPK_GUARD_DAC_MUTE);
		assert(spk_guard_step(&g, 1, 0, 100 + 350) == SPK_GUARD_AMP_ON);
		// Codec drops before the unmute window: amp off first (DAC still muted).
		assert(spk_guard_step(&g, 0, 0, 100 + 400) == SPK_GUARD_AMP_OFF);
		// Next poll: restore the DAC so it is never left muted.
		assert(spk_guard_step(&g, 0, 0, 100 + 450) == SPK_GUARD_DAC_RESTORE);
		assert(spk_guard_step(&g, 0, DAC, 100 + 500) == SPK_GUARD_NONE);
	}

	// --- volume 0 at power-up: nothing until dac>0, then the full sequence ---
	{
		SpkGuard g;
		spk_guard_init(&g);
		assert(spk_guard_step(&g, 0, 0, 0) == SPK_GUARD_AMP_OFF);
		// Codec powers up but the user's volume is 0: stay muted (hiss kill).
		assert(spk_guard_step(&g, 1, 0, 100) == SPK_GUARD_NONE);
		assert(spk_guard_step(&g, 1, 0, 100 + 500) == SPK_GUARD_NONE);
		// User raises the volume mid-stream: run the silent enable sequence.
		assert(spk_guard_step(&g, 1, DAC, 2000) == SPK_GUARD_DAC_MUTE);
		assert(spk_guard_step(&g, 1, 0, 2000 + 350) == SPK_GUARD_AMP_ON);
		assert(spk_guard_step(&g, 1, 0, 2000 + 500) == SPK_GUARD_DAC_RESTORE);
		assert(spk_guard_step(&g, 1, DAC, 2000 + 700) == SPK_GUARD_NONE);
	}

	// --- volume set to 0 while ACTIVE: AMP_OFF, then dac>0 re-runs the sequence -
	{
		SpkGuard g;
		spk_guard_init(&g);
		assert(spk_guard_step(&g, 0, DAC, 0) == SPK_GUARD_AMP_OFF);
		assert(spk_guard_step(&g, 1, DAC, 100) == SPK_GUARD_DAC_MUTE);
		assert(spk_guard_step(&g, 1, 0, 100 + 350) == SPK_GUARD_AMP_ON);
		assert(spk_guard_step(&g, 1, 0, 100 + 500) == SPK_GUARD_DAC_RESTORE);
		assert(spk_guard_step(&g, 1, DAC, 100 + 700) == SPK_GUARD_NONE); // ACTIVE
		// User drops volume to 0: mute the amp to kill the hiss.
		assert(spk_guard_step(&g, 1, 0, 100 + 800) == SPK_GUARD_AMP_OFF);
		assert(spk_guard_step(&g, 1, 0, 100 + 850) == SPK_GUARD_NONE); // idle, muted
		// User raises it again: full silent enable sequence once more.
		assert(spk_guard_step(&g, 1, DAC, 100 + 900) == SPK_GUARD_DAC_MUTE);
		assert(spk_guard_step(&g, 1, 0, 100 + 900 + 350) == SPK_GUARD_AMP_ON);
		assert(spk_guard_step(&g, 1, 0, 100 + 900 + 500) == SPK_GUARD_DAC_RESTORE);
	}

	// --- read errors (-1) hold state ---
	{
		SpkGuard g;
		spk_guard_init(&g);
		// pm error before any valid read: no forced state.
		assert(spk_guard_step(&g, -1, DAC, 0) == SPK_GUARD_NONE);
		assert(spk_guard_step(&g, 0, DAC, 10) == SPK_GUARD_AMP_OFF);
		assert(spk_guard_step(&g, 1, DAC, 100) == SPK_GUARD_DAC_MUTE);
		// pm error mid power-up must not advance/reset the settle timer.
		assert(spk_guard_step(&g, -1, 0, 200) == SPK_GUARD_NONE);
		assert(spk_guard_step(&g, 1, 0, 100 + 350) == SPK_GUARD_AMP_ON);
		assert(spk_guard_step(&g, 1, 0, 100 + 500) == SPK_GUARD_DAC_RESTORE);
		// A DAC read error while ACTIVE must not be treated as volume 0.
		assert(spk_guard_step(&g, 1, -1, 100 + 700) == SPK_GUARD_NONE);
		assert(spk_guard_step(&g, 1, DAC, 100 + 800) == SPK_GUARD_NONE);
	}

	// --- repeated cycles: one full quartet per power cycle ---
	{
		SpkGuard g;
		spk_guard_init(&g);
		assert(spk_guard_step(&g, 0, DAC, 0) == SPK_GUARD_AMP_OFF);
		long long t = 0;
		for (int cycle = 0; cycle < 3; cycle++) {
			t += 1000;
			assert(spk_guard_step(&g, 1, DAC, t) == SPK_GUARD_DAC_MUTE);
			assert(spk_guard_step(&g, 1, 0, t + 200) == SPK_GUARD_NONE);
			assert(spk_guard_step(&g, 1, 0, t + 350) == SPK_GUARD_AMP_ON);
			assert(spk_guard_step(&g, 1, 0, t + 450) == SPK_GUARD_NONE);
			assert(spk_guard_step(&g, 1, 0, t + 500) == SPK_GUARD_DAC_RESTORE);
			assert(spk_guard_step(&g, 1, DAC, t + 600) == SPK_GUARD_NONE);
			t += 800;
			assert(spk_guard_step(&g, 0, DAC, t) == SPK_GUARD_AMP_OFF);
			assert(spk_guard_step(&g, 0, DAC, t + 50) == SPK_GUARD_NONE);
		}
	}

	printf("test_spk_guard: all tests passed\n");
	return 0;
}
