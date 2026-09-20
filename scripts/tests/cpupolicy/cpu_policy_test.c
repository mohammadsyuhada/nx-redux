// Host unit test: the launcher's CPU frequency policy state machine
// (workspace/all/nextui/cpu_policy.c). Boot phase runs the full range until
// launch.sh's marker appears (or a fallback timer), then the menu cap while
// navigating and the idle cap after CPU_POLICY_IDLE_MS without input.
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "cpu_policy.h"

static int failures = 0;
#define CHECK(cond, ...)                                \
	do {                                                \
		if (!(cond)) {                                  \
			failures++;                                 \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__);                        \
			printf("\n");                               \
		}                                               \
	} while (0)

static int marker_calls = 0;
static bool marker_present = false;
static bool marker(void) {
	marker_calls++;
	return marker_present;
}

static void test_relaunch_skips_boot_phase(void) {
	CPUPolicy p;
	CPUPolicyAction a = CPUPolicy_start(&p, 1000, true);
	CHECK(a == CPU_POLICY_SET_MENU, "marker already present: first frame applies the menu cap (got %d)", a);
	CHECK(p.state == CPU_POLICY_ACTIVE, "marker already present: state is ACTIVE");
}

static void test_boot_phase_min_duration_then_marker_at_rate_limit(void) {
	CPUPolicy p;
	marker_calls = 0;
	marker_present = true; // marker lands early: must still wait for the minimum
	const uint32_t t0 = 1000, tmin = t0 + CPU_POLICY_BOOT_MIN_MS;
	CPUPolicyAction a = CPUPolicy_start(&p, t0, false);
	CHECK(a == CPU_POLICY_SET_AUTO, "no marker at start: lift the cap (got %d)", a);
	CHECK(p.state == CPU_POLICY_BOOT, "no marker at start: state is BOOT");
	for (uint32_t t = t0 + 16; t < tmin; t += 16)
		CHECK(CPUPolicy_update(&p, t, true, marker) == CPU_POLICY_KEEP, "boot: nothing to apply before the minimum (t=%u)", t);
	CHECK(marker_calls == 0, "boot: marker never stat'd before the minimum (calls=%d)", marker_calls);
	// at the minimum the marker is consulted and, present, ends the phase
	// (a button was held throughout -> menu cap, not idle)
	a = CPUPolicy_update(&p, tmin, true, marker);
	CHECK(marker_calls == 1, "boot: first stat at the minimum (calls=%d)", marker_calls);
	CHECK(a == CPU_POLICY_SET_MENU, "boot done with recent input: menu cap (got %d)", a);
	CHECK(p.state == CPU_POLICY_ACTIVE, "boot done: ACTIVE");
}

static void test_boot_phase_marker_polling_is_rate_limited(void) {
	CPUPolicy p;
	marker_calls = 0;
	marker_present = false;
	const uint32_t t0 = 0, tmin = CPU_POLICY_BOOT_MIN_MS;
	CPUPolicy_start(&p, t0, false);
	CHECK(CPUPolicy_update(&p, tmin, false, marker) == CPU_POLICY_KEEP, "boot: marker absent at the minimum, keep waiting");
	CHECK(marker_calls == 1, "boot: one stat at the minimum (calls=%d)", marker_calls);
	for (uint32_t t = tmin + 16; t < tmin + CPU_POLICY_BOOT_CHECK_MS; t += 16)
		CPUPolicy_update(&p, t, false, marker);
	CHECK(marker_calls == 1, "boot: no re-stat inside the check interval (calls=%d)", marker_calls);
	marker_present = true;
	CPUPolicyAction a = CPUPolicy_update(&p, tmin + CPU_POLICY_BOOT_CHECK_MS, false, marker);
	CHECK(marker_calls == 2, "boot: second stat one interval later (calls=%d)", marker_calls);
	CHECK(a == CPU_POLICY_SET_IDLE, "boot done, nobody pressed anything: idle cap (got %d)", a);
}

static void test_boot_done_while_idle_goes_straight_to_idle_cap(void) {
	CPUPolicy p;
	marker_present = true;
	CPUPolicy_start(&p, 0, false);
	// no input for the whole boot phase
	CPUPolicyAction a = CPUPolicy_update(&p, CPU_POLICY_BOOT_MIN_MS, false, marker);
	CHECK(a == CPU_POLICY_SET_IDLE, "boot done, user idle: idle cap (got %d)", a);
	CHECK(p.state == CPU_POLICY_IDLE, "boot done, user idle: IDLE");
}

static void test_boot_fallback_timer(void) {
	CPUPolicy p;
	marker_present = false;
	CPUPolicy_start(&p, 5000, false);
	CPUPolicyAction a = CPU_POLICY_KEEP;
	uint32_t t;
	for (t = 5016; t < 5000 + CPU_POLICY_BOOT_FALLBACK_MS; t += 16)
		a = CPUPolicy_update(&p, t, true, marker); // holding a button the whole time
	CHECK(a == CPU_POLICY_KEEP, "fallback: still boot just before the timer");
	a = CPUPolicy_update(&p, 5000 + CPU_POLICY_BOOT_FALLBACK_MS, true, marker);
	CHECK(a == CPU_POLICY_SET_MENU, "fallback: menu cap at the timer even without a marker (got %d)", a);
	CHECK(CPUPolicy_update(&p, 5000 + CPU_POLICY_BOOT_FALLBACK_MS + 16, true, marker) == CPU_POLICY_KEEP, "fallback: applied once");
}

static void test_boot_without_marker_probe(void) {
	CPUPolicy p;
	CPUPolicy_start(&p, 0, false);
	CHECK(CPUPolicy_update(&p, CPU_POLICY_BOOT_MIN_MS, false, NULL) == CPU_POLICY_KEEP, "NULL probe tolerated");
	CHECK(CPUPolicy_update(&p, CPU_POLICY_BOOT_FALLBACK_MS, true, NULL) == CPU_POLICY_SET_MENU, "NULL probe: fallback still ends boot");
}

static void test_idle_drop_and_wake(void) {
	CPUPolicy p;
	CPUPolicy_start(&p, 0, true);
	CPUPolicy_update(&p, 100, true, marker);
	CHECK(CPUPolicy_update(&p, 100 + CPU_POLICY_IDLE_MS - 1, false, marker) == CPU_POLICY_KEEP, "active: no drop before the idle timeout");
	CPUPolicyAction a = CPUPolicy_update(&p, 100 + CPU_POLICY_IDLE_MS, false, marker);
	CHECK(a == CPU_POLICY_SET_IDLE, "active: idle cap at the timeout (got %d)", a);
	CHECK(CPUPolicy_update(&p, 100 + CPU_POLICY_IDLE_MS + 16, false, marker) == CPU_POLICY_KEEP, "idle: applied once");
	a = CPUPolicy_update(&p, 100 + CPU_POLICY_IDLE_MS + 5000, true, marker);
	CHECK(a == CPU_POLICY_SET_MENU, "idle: input restores the menu cap in the same call (got %d)", a);
	CHECK(p.state == CPU_POLICY_ACTIVE, "idle: input -> ACTIVE");
	CHECK(CPUPolicy_update(&p, 100 + CPU_POLICY_IDLE_MS + 5016, false, marker) == CPU_POLICY_KEEP, "active again: nothing more to apply");
}

static void test_input_keeps_active(void) {
	CPUPolicy p;
	CPUPolicy_start(&p, 0, true);
	for (uint32_t t = 16; t < 20000; t += 16)
		CHECK(CPUPolicy_update(&p, t, (t % 2000) == 0, marker) == CPU_POLICY_KEEP, "a press every 2 s never drops to idle (t=%u)", t);
}

int main(void) {
	test_relaunch_skips_boot_phase();
	test_boot_phase_min_duration_then_marker_at_rate_limit();
	test_boot_phase_marker_polling_is_rate_limited();
	test_boot_done_while_idle_goes_straight_to_idle_cap();
	test_boot_fallback_timer();
	test_boot_without_marker_probe();
	test_idle_drop_and_wake();
	test_input_keeps_active();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("cpu_policy: all tests passed\n");
	return 0;
}
