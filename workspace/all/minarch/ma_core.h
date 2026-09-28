#pragma once

struct Cheats;

void Core_open(const char* core_path, const char* tag_name);
void Core_init(void);
void Core_applyCheats(struct Cheats* cheats);
int Core_updateAVInfo(void);
void Core_setPendingAVInfo(const struct retro_system_av_info* av);
void Core_setPendingGeometry(const struct retro_game_geometry* geometry);
void Core_applyPendingAV(void);
void Core_load(void);
void Core_reset(void);
void Core_unload(void);
void Core_quit(void);
void Core_close(void);
