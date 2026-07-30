#ifndef MA_OPTS_SCHEMA_H
#define MA_OPTS_SCHEMA_H

#include "libretro.h"

// Serializers for the pre-launch options schema cache (options.json), the
// dialect emu_overlay_cfg.c parses. Pure functions over the core's RAW option
// definitions — captured before OptionList_init wraps info text against the
// device's screen geometry. Each returns a malloc'd JSON string (caller
// frees), or NULL when there is nothing to serialize.
char* OptsSchema_fromV1(const struct retro_core_option_definition* defs, const char* emulator);
char* OptsSchema_fromV2(const struct retro_core_options_v2* v2, const char* emulator);
char* OptsSchema_fromVars(const struct retro_variable* vars, const char* emulator);

// Write-if-changed, atomic (tmp + rename + sync). 0 on written-or-unchanged,
// -1 on I/O error.
int OptsSchema_writeFile(const char* path, const char* json);

// Caps tied to the pre-launch editor's raised limits — keep in lockstep with
// the -DEMU_OVL_MAX_* values in workspace/all/emu-options/Makefile.
#define OPTS_SCHEMA_MAX_SECTIONS 32
#define OPTS_SCHEMA_MAX_ITEMS_PER_SECTION 64
#define OPTS_SCHEMA_MAX_VALUES 32

#endif
