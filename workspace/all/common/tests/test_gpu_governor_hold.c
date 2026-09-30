// Host unit test for gpu_governor_hold.h: minarch's in-game menu drops the tg5050
// GPU to simple_ondemand, and the game speeds must put back the governor the pak's
// launch.sh chose (performance for DC/PS/PSP) instead of leaving it throttled
// (simple_ondemand sat at 150 MHz under PPSSPP: Tekken 6 at 2x fell to 88%).
// Built with ASan by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../gpu_governor_hold.h"

int main(void) {
	GpuGovHold h = {0};

	// nothing held yet: a game speed has nothing to restore
	assert(GpuGovHold_leaveMenu(&h) == NULL);

	// entering the menu remembers the pak's governor (sysfs value ends in '\n')
	assert(strcmp(GpuGovHold_enterMenu(&h, "performance\n"), "simple_ondemand") == 0);
	// menu -> menu idle -> menu keeps the first value, not the menu's own governor
	assert(strcmp(GpuGovHold_enterMenu(&h, "simple_ondemand\n"), "simple_ondemand") == 0);

	// back in the game: restore it once
	const char* restore = GpuGovHold_leaveMenu(&h);
	assert(restore && strcmp(restore, "performance") == 0);
	assert(GpuGovHold_leaveMenu(&h) == NULL);

	// a pak that never set the governor: restore what it had
	assert(strcmp(GpuGovHold_enterMenu(&h, "simple_ondemand"), "simple_ondemand") == 0);
	restore = GpuGovHold_leaveMenu(&h);
	assert(restore && strcmp(restore, "simple_ondemand") == 0);

	// unreadable sysfs (empty): nothing to restore later
	assert(strcmp(GpuGovHold_enterMenu(&h, ""), "simple_ondemand") == 0);
	assert(GpuGovHold_leaveMenu(&h) == NULL);

	// an over-long value is truncated safely, never overflows
	char big[128];
	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	GpuGovHold_enterMenu(&h, big);
	restore = GpuGovHold_leaveMenu(&h);
	assert(restore && strlen(restore) < sizeof(h.saved));

	printf("test_gpu_governor_hold: ALL PASS\n");
	return 0;
}
