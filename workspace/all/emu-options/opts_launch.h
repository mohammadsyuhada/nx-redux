#ifndef OPTS_LAUNCH_H
#define OPTS_LAUNCH_H

#include "emu_overlay_cfg.h"

// --launch mode's flat page: every item of every visible section on one
// screen, ahead of a Launch row.

// Fills out_sec/out_item with the (section, item) index pair of each item in
// each section emu_ovl_cfg_section_visible() lists, in schema order. Returns
// the pair count, capped at max. Visibility reads STAGED values, so call it
// again after every edit: a visible_when gate can add or drop rows.
int opts_launch_collect(const EmuOvlConfig* cfg, int* out_sec, int* out_item, int max);

#endif
