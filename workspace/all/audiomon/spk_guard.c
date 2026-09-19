// spk_guard.c — see spk_guard.h. Pure logic, no ALSA/system includes.
#include "spk_guard.h"

void spk_guard_init(SpkGuard* g) {
	g->pm_on = 0;
	g->amp_on = -1; // unknown until the first valid power read
	g->phase = SPK_PHASE_IDLE;
	g->dac_muted = 0;
	g->saved_dac = 0;
	g->t_mute_ms = 0;
	g->t_amp_ms = 0;
}

SpkGuardAction spk_guard_step(SpkGuard* g, int pm_on_now, int dac_now, long long now_ms) {
	// Read error (-1 pm): hold state, change nothing.
	if (pm_on_now != 0 && pm_on_now != 1)
		return SPK_GUARD_NONE;

	// First valid step: force the amp known-muted, leave the DAC alone.
	if (g->amp_on == -1) {
		g->amp_on = 0;
		g->phase = SPK_PHASE_IDLE;
		g->pm_on = pm_on_now;
		return SPK_GUARD_AMP_OFF;
	}

	switch (g->phase) {
	case SPK_PHASE_IDLE:
		// Amp off. First settle any DAC left muted by a power-down.
		if (g->dac_muted) {
			g->dac_muted = 0;
			g->pm_on = pm_on_now;
			return SPK_GUARD_DAC_RESTORE;
		}
		// Begin the silent enable only for a powered codec with real volume.
		// dac_now == 0 (user volume 0) or -1 (read error) leaves the amp muted.
		if (pm_on_now == 1 && dac_now > 0) {
			g->saved_dac = dac_now;
			g->dac_muted = 1;
			g->phase = SPK_PHASE_MUTING;
			g->t_mute_ms = now_ms;
			g->pm_on = 1;
			return SPK_GUARD_DAC_MUTE;
		}
		g->pm_on = pm_on_now;
		return SPK_GUARD_NONE;

	case SPK_PHASE_MUTING:
		if (pm_on_now == 0) {
			// Codec dropped before the amp came on; restore the DAC (amp off).
			g->dac_muted = 0;
			g->phase = SPK_PHASE_IDLE;
			g->pm_on = 0;
			return SPK_GUARD_DAC_RESTORE;
		}
		if (now_ms - g->t_mute_ms >= SPK_GUARD_SETTLE_MS) {
			g->amp_on = 1;
			g->phase = SPK_PHASE_AMP_ON_WAIT;
			g->t_amp_ms = now_ms;
			g->pm_on = 1;
			return SPK_GUARD_AMP_ON;
		}
		g->pm_on = 1;
		return SPK_GUARD_NONE;

	case SPK_PHASE_AMP_ON_WAIT:
		if (pm_on_now == 0) {
			// Codec dropped after the amp came on: force it off now; the DAC
			// restore follows next poll (dac_muted is still set).
			g->amp_on = 0;
			g->phase = SPK_PHASE_IDLE;
			g->pm_on = 0;
			return SPK_GUARD_AMP_OFF;
		}
		if (now_ms - g->t_amp_ms >= SPK_GUARD_UNMUTE_MS) {
			g->dac_muted = 0;
			g->phase = SPK_PHASE_ACTIVE;
			g->pm_on = 1;
			return SPK_GUARD_DAC_RESTORE;
		}
		g->pm_on = 1;
		return SPK_GUARD_NONE;

	case SPK_PHASE_ACTIVE:
		if (pm_on_now == 0) {
			// Codec powered down: force the amp off (silent, codec is down).
			g->amp_on = 0;
			g->phase = SPK_PHASE_IDLE;
			g->pm_on = 0;
			return SPK_GUARD_AMP_OFF;
		}
		if (dac_now == 0) {
			// User dropped the volume to 0: mute the amp (kill the hiss). A
			// later dac>0 re-runs the silent enable sequence from IDLE.
			g->amp_on = 0;
			g->phase = SPK_PHASE_IDLE;
			g->pm_on = 1;
			return SPK_GUARD_AMP_OFF;
		}
		g->pm_on = 1;
		return SPK_GUARD_NONE;
	}

	return SPK_GUARD_NONE;
}
