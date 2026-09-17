#ifndef MA_OPTS_DUMP_H
#define MA_OPTS_DUMP_H

#include "libretro.h"

// Launch-time cache refresh: serialize the registering core's raw defs to
// <core.config_dir>/options.json (write-if-changed). Called from
// environment_callback BEFORE OptionList_* consumes the defs.
void OptsCache_refreshV1(const struct retro_core_option_definition* defs);
void OptsCache_refreshV2(const struct retro_core_options_v2* v2);
void OptsCache_refreshVars(const struct retro_variable* vars);

// `minarch.elf --dump-options <core.so> <out.json> [system_dir]`:
// self-contained dlopen dump — own minimal environment callback, no
// Core_open/GFX/config init. `system_dir` is handed to the core for
// GET_SYSTEM_DIRECTORY so cores that build option lists by scanning 'system'
// (e.g. PUAE's Kickstart ROM list) see the real BIOS dir instead of /tmp;
// pass NULL/empty to fall back to /tmp. Returns 0 when a schema was written
// (options.sh treats non-zero as "keep whatever cache already exists").
int OptsDump_run(const char* core_path, const char* out_json_path, const char* system_dir);

#endif
