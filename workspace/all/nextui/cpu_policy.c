#include "cpu_policy.h"

static bool CPUPolicy_idleElapsed(const CPUPolicy* p, uint32_t now) {
	return (uint32_t)(now - p->last_input) >= CPU_POLICY_IDLE_MS;
}

// Leaving BOOT lands on whichever steady state the input history calls for:
// a boot that ended with the user already idle caps straight to the idle
// speed instead of spending CPU_POLICY_IDLE_MS more at the menu cap.
static CPUPolicyAction CPUPolicy_leaveBoot(CPUPolicy* p, uint32_t now) {
	if (CPUPolicy_idleElapsed(p, now)) {
		p->state = CPU_POLICY_IDLE;
		return CPU_POLICY_SET_IDLE;
	}
	p->state = CPU_POLICY_ACTIVE;
	return CPU_POLICY_SET_MENU;
}

CPUPolicyAction CPUPolicy_start(CPUPolicy* p, uint32_t now, bool boot_done) {
	p->boot_start = now;
	p->next_boot_check = now + CPU_POLICY_BOOT_CHECK_MS;
	p->last_input = now;
	if (boot_done) {
		p->state = CPU_POLICY_ACTIVE;
		return CPU_POLICY_SET_MENU;
	}
	p->state = CPU_POLICY_BOOT;
	return CPU_POLICY_SET_AUTO;
}

CPUPolicyAction CPUPolicy_update(CPUPolicy* p, uint32_t now, bool input, bool (*boot_done)(void)) {
	if (input)
		p->last_input = now;

	switch (p->state) {
	case CPU_POLICY_BOOT: {
		uint32_t elapsed = now - p->boot_start;
		if (elapsed >= CPU_POLICY_BOOT_FALLBACK_MS)
			return CPUPolicy_leaveBoot(p, now);
		if (elapsed < CPU_POLICY_BOOT_MIN_MS || (int32_t)(now - p->next_boot_check) < 0)
			return CPU_POLICY_KEEP;
		p->next_boot_check = now + CPU_POLICY_BOOT_CHECK_MS;
		if (boot_done && boot_done())
			return CPUPolicy_leaveBoot(p, now);
		return CPU_POLICY_KEEP;
	}
	case CPU_POLICY_ACTIVE:
		if (CPUPolicy_idleElapsed(p, now)) {
			p->state = CPU_POLICY_IDLE;
			return CPU_POLICY_SET_IDLE;
		}
		return CPU_POLICY_KEEP;
	case CPU_POLICY_IDLE:
		if (input) {
			p->state = CPU_POLICY_ACTIVE;
			return CPU_POLICY_SET_MENU;
		}
		return CPU_POLICY_KEEP;
	}
	return CPU_POLICY_KEEP;
}
