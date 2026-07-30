#ifndef MA_OPTS_DUMP_H
#define MA_OPTS_DUMP_H

#include "libretro.h"

// Launch-time cache refresh: serialize the registering core's raw defs to
// <core.config_dir>/options.json (write-if-changed). Called from
// environment_callback BEFORE OptionList_* consumes the defs.
void OptsCache_refreshV1(const struct retro_core_option_definition* defs);
void OptsCache_refreshV2(const struct retro_core_options_v2* v2);
void OptsCache_refreshVars(const struct retro_variable* vars);

// `minarch.elf --dump-options <core.so> <out.json>`: self-contained dlopen
// dump — own minimal environment callback, no Core_open/GFX/config init.
// Returns 0 when a schema was written (options.sh treats non-zero as "keep
// whatever cache already exists").
int OptsDump_run(const char* core_path, const char* out_json_path);

#endif
