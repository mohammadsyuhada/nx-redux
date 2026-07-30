#ifndef OPTS_MINARCH_H
#define OPTS_MINARCH_H

#include "emu_overlay_cfg.h"
#include "opts_override.h"

/*
 * minarch storage backend: flat "key = value" cfg files, layered
 * system -> pak default -> user tier, where the user tier is EITHER
 * <config_dir>/minarch[-DEVICE].cfg (console) OR
 * <config_dir>/<alt_name>[-DEVICE].cfg (per-game) — the game file REPLACES
 * the console file, it does not stack (ma_config.c:1383-1396). The user-tier
 * filename is strictly device-suffixed when $DEVICE is set, no fallback
 * (Config_getPath, ma_config.c:1113-1121); system/default layers DO fall
 * back, but the SHELL resolves those two paths and passes them in.
 * Lines "-key = value" are locked: those keys are pruned from the schema
 * (hidden from the editor) and their lines pass through writes untouched.
 */
typedef struct {
	char console_path[1024];
	char game_path[1024];
	bool per_game;
	bool game_existed;
	// console-tier effective value (system -> default -> console) per item;
	// also mirrored into OptsOverrideState.global_value for the shared UI
	int console_value[EMU_OVL_MAX_SECTIONS][EMU_OVL_MAX_ITEMS];
	char* console_content; // raw console file bytes, NULL when absent
	char* game_content;	   // raw game file bytes, NULL when absent
} OptsMinarchState;

// Load layers into cfg (current_value/staged_value = launch-effective
// values), prune locked keys and then empty sections, fill ost with the
// console baseline. system/default paths may be NULL or missing (empty
// layers). alt_name NULL = global mode. Returns 0; -1 when config_dir is
// NULL/oversized.
int opts_minarch_load(EmuOvlConfig* cfg, OptsMinarchState* st, OptsOverrideState* ost,
					  const char* system_cfg_path, const char* default_cfg_path,
					  const char* config_dir, const char* alt_name);

// The single write on exit.
// Global: minimal-diff rewrite of the console file (only keys whose staged
// value differs from the loaded effective value; unrelated lines byte-for-
// byte; missing keys appended; file created when absent).
// Per-game: candidate = (existing game file, else console file, else empty)
// with EVERY schema key set to its staged value; reference = console file
// with every schema key at its console value. candidate == reference ->
// unlink/skip (fully redundant), else write candidate. Atomic (tmp+rename),
// sync()s. Returns 0 on success (including redundant-unlink), -1 on I/O
// error.
int opts_minarch_save(EmuOvlConfig* cfg, OptsMinarchState* st, const OptsOverrideState* ost);

void opts_minarch_free(OptsMinarchState* st);

#endif
