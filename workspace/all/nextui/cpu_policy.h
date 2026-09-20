#ifndef CPU_POLICY_H
#define CPU_POLICY_H

#include <stdbool.h>
#include <stdint.h>

// Launcher CPU frequency policy — a pure state machine (host test:
// scripts/tests/test-cpu-policy.sh), wired to PWR_setCPUSpeed* in nextui.c.
// The caps themselves live in each platform's PLAT_setCPUSpeed.
//
//   BOOT    full range while the system is still booting. It ends once BOTH
//           CPU_POLICY_BOOT_MIN_MS have passed and launch.sh has touched
//           CPU_POLICY_BOOT_MARKER (its bt/wifi/ssh init jobs have all exited;
//           polled every CPU_POLICY_BOOT_CHECK_MS), or at the
//           CPU_POLICY_BOOT_FALLBACK_MS timer if a script hangs. The minimum
//           exists because the marker lands ~5 s after launcher start while the
//           stock init (procd) and SD I/O keep the system 30-40% busy for
//           ~12-14 s (Brick, 2026-09-20). The phase is skipped when the marker
//           already exists — a relaunch after a game or a tool (the marker
//           lives on tmpfs, so it is gone again on the next boot). Capped, the
//           glide in that window took 27 ms median / 40 ms worst vs 20 / 31
//           uncapped.
//   ACTIVE  CPU_SPEED_MENU while the user is navigating. On tg5050 the big
//           core is also taken offline for ACTIVE and IDLE (the UI is
//           little-bound; PLAT_setBigCoreOnline) and brought back by SET_AUTO.
//   IDLE    CPU_SPEED_MENU_IDLE after CPU_POLICY_IDLE_MS without input. schedutil
//           on these kernels parks at scaling_max_freq when idle, so the cap is
//           the idle operating point — this is what actually lowers idle draw.
//           Any input returns to ACTIVE in the same poll, before the frame
//           renders (a sysfs max write takes effect immediately; unlike
//           ondemand there is no ramp latency on the first frame).
#define CPU_POLICY_BOOT_MARKER "/tmp/nx_boot_done"
#define CPU_POLICY_BOOT_MIN_MS 12000
#define CPU_POLICY_BOOT_FALLBACK_MS 20000
#define CPU_POLICY_BOOT_CHECK_MS 500
#define CPU_POLICY_IDLE_MS 3000

typedef enum {
	CPU_POLICY_BOOT,
	CPU_POLICY_ACTIVE,
	CPU_POLICY_IDLE,
} CPUPolicyState;

typedef enum {
	CPU_POLICY_KEEP,	 // nothing to apply this frame
	CPU_POLICY_SET_AUTO, // PWR_setCPUSpeedAuto()
	CPU_POLICY_SET_MENU, // PWR_setCPUSpeed(CPU_SPEED_MENU)
	CPU_POLICY_SET_IDLE, // PWR_setCPUSpeed(CPU_SPEED_MENU_IDLE)
} CPUPolicyAction;

typedef struct {
	CPUPolicyState state;
	uint32_t boot_start;
	uint32_t next_boot_check;
	uint32_t last_input;
} CPUPolicy;

// Called once: at the top of main when the marker is absent (so InitSettings,
// GFX_init, menu init and a first-boot ROM rescan all run at full range), at
// the first frame otherwise. `boot_done` = marker already exists.
CPUPolicyAction CPUPolicy_start(CPUPolicy* p, uint32_t now, bool boot_done);

// Once per main-loop iteration, after input polling. `input` is "any button
// held this poll". `boot_done` is called (rate-limited) only during BOOT to
// stat the marker (never before CPU_POLICY_BOOT_MIN_MS); may be NULL (the
// fallback timer alone then ends the phase).
CPUPolicyAction CPUPolicy_update(CPUPolicy* p, uint32_t now, bool input, bool (*boot_done)(void));

#endif
