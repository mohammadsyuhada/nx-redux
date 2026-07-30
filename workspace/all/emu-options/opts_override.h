#ifndef OPTS_OVERRIDE_H
#define OPTS_OVERRIDE_H

#include "emu_overlay_cfg.h"

// Per-game override layer on top of the device-global emu.cfg.
// Lifecycle: load JSON -> read_ini(emu.cfg) -> opts_snapshot_globals ->
// opts_read_override -> user edits staged_value -> opts_write_override ->
// opts_commit.

typedef struct {
	// current_value of every item right after reading the global INI
	int global_value[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS];
	// key was present in the override file (drives the UI's override marker)
	bool overridden[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS];
} OptsOverrideState;

// Snapshot every item's current_value as the global baseline. Call after
// emu_ovl_cfg_read_ini(cfg, <emu.cfg>).
void opts_snapshot_globals(const EmuOvlConfig* cfg, OptsOverrideState* st);

// Layer an override INI on top: for each recognized [section] key = value,
// set the item's current_value and staged_value and mark st->overridden.
// Unknown sections/keys and unparseable values are skipped. Missing or
// unreadable file is a no-op. Returns the number of keys applied.
int opts_read_override(EmuOvlConfig* cfg, OptsOverrideState* st, const char* override_path);

// Write the staged-vs-global diff as a minimal INI (sections in first-seen
// resolved-name order, "key = value" lines matching emu.cfg's dialect).
// When no item differs from its global baseline the file is unlinked
// instead. Returns the number of keys written (0 after an unlink), -1 on
// I/O error.
int opts_write_override(EmuOvlConfig* cfg, const OptsOverrideState* st, const char* override_path);

// After a successful opts_write_override: refresh st->overridden from the
// staged-vs-global diff and commit staged -> current.
void opts_commit(EmuOvlConfig* cfg, OptsOverrideState* st);

#endif
