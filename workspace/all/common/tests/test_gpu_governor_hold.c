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

	// shader boost (an opted-in pak): launch.sh boosted to performance from simple_ondemand
	GpuGovHold b = {0};
	// the game starts with a shader on: performance, written at once
	const char* gov = GpuGovHold_setBoost(&b, 1, "simple_ondemand\n");
	assert(gov && strcmp(gov, "performance") == 0);
	// the in-game menu still drops to simple_ondemand, and leaving it boosts again
	assert(strcmp(GpuGovHold_enterMenu(&b, "performance\n"), "simple_ondemand") == 0);
	gov = GpuGovHold_leaveMenu(&b);
	assert(gov && strcmp(gov, "performance") == 0);
	// shader turned off in the menu: nothing written yet, and leaving restores the base, not the
	// performance the menu saw on entry
	GpuGovHold_enterMenu(&b, "performance\n");
	assert(GpuGovHold_setBoost(&b, 0, "simple_ondemand") == NULL);
	gov = GpuGovHold_leaveMenu(&b);
	assert(gov && strcmp(gov, "simple_ondemand") == 0);
	// back on in the menu: performance once the game resumes
	GpuGovHold_enterMenu(&b, "simple_ondemand\n");
	assert(GpuGovHold_setBoost(&b, 1, "simple_ondemand") == NULL);
	gov = GpuGovHold_leaveMenu(&b);
	assert(gov && strcmp(gov, "performance") == 0);
	// off outside a menu: the base, written at once
	gov = GpuGovHold_setBoost(&b, 0, "simple_ondemand");
	assert(gov && strcmp(gov, "simple_ondemand") == 0);
	// a pak whose launch.sh set performance itself (DC/PS/PSP) keeps it with the shader off
	GpuGovHold p = {0};
	assert(GpuGovHold_setBoost(&p, 0, "performance") != NULL);
	GpuGovHold_enterMenu(&p, "performance");
	gov = GpuGovHold_leaveMenu(&p);
	assert(gov && strcmp(gov, "performance") == 0);

	printf("test_gpu_governor_hold: ALL PASS\n");
	return 0;
}
