#pragma once

void setOverclock(int i);
void Config_getPath(char* filename, int override);
void Config_syncFrontend(char* key, int value);
void Config_init(void);
void Config_quit(void);
void Config_load(void);
void Config_free(void);
void Config_readOptions(void);
void Config_reapplyOptions(void);
void Config_readOptionsString(char* cfg);
void Config_readControls(void);
void Config_write(int override);
void Config_restore(void);
char** list_files_in_folder(const char* folderPath, int* fileCount, const char* defaultElement, const char* extensionFilter);
void free_file_list(char** list);
