#include "ma_internal.h" // core.config_dir, core.name (refresh path only)
#include "ma_opts_schema.h"
#include "ma_opts_dump.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cache_write(char* json) {
	if (!json)
		return;
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/options.json", (const char*)core.config_dir);
	OptsSchema_writeFile(path, json);
	free(json);
}
void OptsCache_refreshV1(const struct retro_core_option_definition* defs) {
	cache_write(OptsSchema_fromV1(defs, (const char*)core.name));
}
void OptsCache_refreshV2(const struct retro_core_options_v2* v2) {
	cache_write(OptsSchema_fromV2(v2, (const char*)core.name));
}
void OptsCache_refreshVars(const struct retro_variable* vars) {
	cache_write(OptsSchema_fromVars(vars, (const char*)core.name));
}

// ---- dump mode ----
// Deliberately NOT the real environment_callback: that one reaches into
// input/rumble/RA/video state that does not exist in dump mode. This one
// answers only what option registration needs and refuses everything else.
static const char* dump_out = NULL;
static char dump_emu[256] = "core";
static int dump_wrote = 0;

static void dump_log(enum retro_log_level level, const char* fmt, ...) {
	(void)level;
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

static void dump_write(char* json) {
	if (!json)
		return;
	if (OptsSchema_writeFile(dump_out, json) == 0)
		dump_wrote = 1;
	free(json);
}

static bool dump_env(unsigned cmd, void* data) {
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		if (data)
			*(unsigned*)data = 2;
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
		if (data)
			dump_write(OptsSchema_fromV1((const struct retro_core_option_definition*)data, dump_emu));
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
		const struct retro_core_options_intl* intl = (const struct retro_core_options_intl*)data;
		if (intl && intl->us)
			dump_write(OptsSchema_fromV1(intl->us, dump_emu));
		return true;
	}
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
		if (data)
			dump_write(OptsSchema_fromV2((const struct retro_core_options_v2*)data, dump_emu));
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
		const struct retro_core_options_v2_intl* intl = (const struct retro_core_options_v2_intl*)data;
		if (intl && intl->us)
			dump_write(OptsSchema_fromV2(intl->us, dump_emu));
		return true;
	}
	case RETRO_ENVIRONMENT_SET_VARIABLES:
		if (data)
			dump_write(OptsSchema_fromVars((const struct retro_variable*)data, dump_emu));
		return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
		struct retro_log_callback* cb = (struct retro_log_callback*)data;
		if (cb)
			cb->log = dump_log;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		if (data)
			*(const char**)data = "/tmp";
		return true;
	case RETRO_ENVIRONMENT_GET_LANGUAGE:
		if (data)
			*(unsigned*)data = RETRO_LANGUAGE_ENGLISH;
		return true;
	default:
		return false;
	}
}

int OptsDump_run(const char* core_path, const char* out_json_path) {
	dump_out = out_json_path;
	void* handle = dlopen(core_path, RTLD_LAZY);
	if (!handle) {
		fprintf(stderr, "dump-options: dlopen failed for %s: %s\n", core_path, dlerror());
		return 1;
	}
	void (*get_system_info)(struct retro_system_info*) = dlsym(handle, "retro_get_system_info");
	void (*set_environment)(retro_environment_t) = dlsym(handle, "retro_set_environment");
	void (*init)(void) = dlsym(handle, "retro_init");
	void (*deinit)(void) = dlsym(handle, "retro_deinit");
	if (!get_system_info || !set_environment || !init || !deinit) {
		fprintf(stderr, "dump-options: %s is missing libretro entry points\n", core_path);
		dlclose(handle);
		return 1;
	}
	struct retro_system_info info = {0};
	get_system_info(&info);
	if (info.library_name)
		snprintf(dump_emu, sizeof(dump_emu), "%s", info.library_name);
	// registration usually happens inside retro_set_environment; the file is
	// written on capture, so even a crash in retro_init cannot lose it
	set_environment(dump_env);
	init();
	deinit();
	dlclose(handle);
	return dump_wrote ? 0 : 1;
}
