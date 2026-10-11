#include "opts_launch.h"

int opts_launch_collect(const EmuOvlConfig* cfg, int* out_sec, int* out_item, int max) {
	int n = 0;
	for (int s = 0; s < cfg->section_count && n < max; s++) {
		// The predicate takes a non-const config only because its item lookup
		// returns a mutable pointer; it reads and never writes.
		if (!emu_ovl_cfg_section_visible((EmuOvlConfig*)cfg, s))
			continue;
		for (int i = 0; i < cfg->sections[s].item_count && n < max; i++) {
			out_sec[n] = s;
			out_item[n] = i;
			n++;
		}
	}
	return n;
}
