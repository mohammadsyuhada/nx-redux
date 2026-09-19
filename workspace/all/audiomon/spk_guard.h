// spk_guard.h — anti-pop speaker sequencer (Smart Pro S / tg5050).
//
// Pure logic, no ALSA/system includes, so it builds and runs on the host (see
// tests/test_spk_guard.c). The real amp gate on tg5050 is the kernel node
// /sys/class/speaker/mute (write 1 = force the amp OFF, write 0 = amp ON); it
// overrides the DAPM "SPK Switch" in both directions. Two things pop, both
// verified on hardware with a game playing:
//   1. the codec powering UP while the amp is enabled (node 0);
//   2. enabling the amp (node 0) while the DAC is already pushing live signal.
// Powering DOWN, and enabling the amp into a MUTED DAC, are both silent. So per
// power-up we run: DAC_MUTE ("DAC Volume" 0, after saving it) -> wait 350 ms for
// the codec power-up to settle -> AMP_ON (node 0, into silence) -> wait 150 ms
// for the amp to settle -> DAC_RESTORE (write the saved volume back in one step)
// -> ACTIVE. On power-down the amp is forced off (node 1) and the DAC restored,
// both silent. At user volume 0 the amp is left muted (kills the idle hiss).
#ifndef SPK_GUARD_H
#define SPK_GUARD_H

typedef enum {
	SPK_GUARD_NONE = 0,
	SPK_GUARD_DAC_MUTE,	   // write "DAC Volume" 0 (value already saved by the step)
	SPK_GUARD_AMP_ON,	   // /sys/class/speaker/mute <- 0 (amp on, into the muted DAC)
	SPK_GUARD_DAC_RESTORE, // write the saved "DAC Volume" back
	SPK_GUARD_AMP_OFF	   // /sys/class/speaker/mute <- 1 (amp forced off)
} SpkGuardAction;

typedef enum {
	SPK_PHASE_IDLE = 0,	   // amp off (node 1), waiting for a stream with volume
	SPK_PHASE_MUTING,	   // DAC muted, waiting out the codec power-up settle
	SPK_PHASE_AMP_ON_WAIT, // amp on, waiting out the amp settle before restoring
	SPK_PHASE_ACTIVE	   // amp on, DAC at the user's level
} SpkGuardPhase;

typedef struct {
	int pm_on;			 // last observed codec power state (0/1)
	int amp_on;			 // wanted amp gate: 1 on (node 0), 0 off (node 1), -1 unknown
	SpkGuardPhase phase; //
	int dac_muted;		 // 1 while the DAC is held at 0 (a restore is owed)
	int saved_dac;		 // user "DAC Volume" captured before muting
	long long t_mute_ms; // monotonic ms when MUTING began (codec power-up)
	long long t_amp_ms;	 // monotonic ms when the amp was enabled
} SpkGuard;

// Proven silent by listening test with a game playing.
#define SPK_GUARD_SETTLE_MS 350 // codec power-up -> amp on
#define SPK_GUARD_UNMUTE_MS 150 // amp on -> DAC volume restored

void spk_guard_init(SpkGuard* g);

// Feed the current codec power state (1 On, 0 Off, else = read error / no change)
// and the current "DAC Volume" (>=0, or -1 on read error) every poll. A step
// returns at most one action; the caller applies it and steps again next poll,
// so the sequence unfolds across polls. While MUTING/AMP_ON_WAIT the DAC reads 0
// (we muted it) — the step never mistakes that for the user's volume.
SpkGuardAction spk_guard_step(SpkGuard* g, int pm_on_now, int dac_now, long long now_ms);

#endif // SPK_GUARD_H
