#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

extern volatile bool app_quit;
void sig_handler(int sig);
void setup_signal_handlers(void);

int prefixMatch(char* pre, const char* str);
int suffixMatch(char* suf, const char* str);
int exactMatch(const char* str1, const char* str2);
int containsString(char* haystack, char* needle);
int hide(char* file_name);

char* splitString(char* str, const char* delim);
char* replaceString2(const char* orig, char* rep, char* with);
int replaceString(char* line, size_t buf_size, const char* search, const char* replace);
char* escapeSingleQuotes(char* str, size_t buf_size);
void truncateString(char* string, size_t max_len);
void wrapString(char* string, size_t max_len, size_t max_lines);
size_t trimString(char* out, size_t len, const char* str, bool first);
void removeParentheses(char* str_out, const char* str_in);
void serializeTime(char* dest_str, int nTime);
// Format duration as HH:MM:SS or MM:SS
void format_time(char* buf, int seconds);
int countChar(const char* str, char ch);
char* removeExtension(const char* myStr);
const char* baseName(const char* filename);
void folderPath(const char* filePath, char* folder_path);
void cleanName(char* name_out, const char* file_name);
bool pathRelativeTo(char* path_out, const char* dir_from, const char* file_to);

void getDisplayName(const char* in_name, char* out_name);
void getEmuName(const char* in_name, char* out_name);
void getEmuPath(char* emu_name, char* pak_path);

void normalizeNewline(char* line);
void trimTrailingNewlines(char* line);
void trimSortingMeta(char** str);

void mkdir_p(const char* path);
bool exists(char* path);
void touch(char* path);
int toggle(char* path); // creates or removes file
void putFile(char* path, char* contents);
int writeFileAtomic(const char* path, const char* data, size_t len); // tmp+fsync+rename; returns 1 on success
// Extract a JSON string value for `key` ("key":"value" or "key": "value") into out; returns out or NULL.
const char* json_extract_string(const char* json, const char* key, char* out, size_t out_size);
char* allocFile(char* path); // caller must free
void getFile(char* path, char* buffer, size_t buffer_size);
// Simple Mode PIN: reads SIMPLE_MODE_PATH's content. Returns true when the
// file holds exactly 4 ASCII digits (trailing whitespace ignored) and copies
// them NUL-terminated into pin_out (pass NULL to just probe). An empty or
// malformed file is a legacy enable flag: simple mode without a PIN gate.
// The length 4 must stay in sync with PINDIALOG_PIN_LEN (ui_pindialog.h).
bool SimpleMode_readPin(char pin_out[5]);
void putInt(char* path, int value);
int getInt(char* path);

// Run `cmd` via popen, capturing stdout into `output` (silently truncated,
// NUL-terminated; `output` may be NULL). Returns the command's exit code via
// WEXITSTATUS, or -1 if popen fails.
int run_cmd_capture(const char* cmd, char* output, size_t output_len);

uint64_t getMicroseconds(void);

int clamp(int x, int lower, int upper);
double clampd(double x, double lower, double upper);

char* findFileInDir(const char* directory, const char* filename);

// Percent-encode src into dst (form style: space -> '+', unreserved kept, rest
// %XX). dst must hold up to 3x src plus a NUL; encoding stops early if it would
// overflow dst_size.
void urlEncode(const char* src, char* dst, size_t dst_size);

// DJB2 over the string's bytes (unsigned); used for cache filenames and hash buckets
unsigned int hashString(const char* str);

// <dir-of-rom>/.media/<basename-without-extension>.png ("." dir if rom_path has no slash)
void ROM_mediaArtPath(const char* rom_path, char* out, size_t out_size);
// ROM_mediaArtPath with a variant subfolder: "screenshot" / "boxart" live in
// <console>/.media/<variant>/<game>.png; NULL or "" gives the root mix path.
void ROM_mediaArtVariantPath(const char* rom_path, const char* variant,
							 char* out, size_t out_size);
// Path the game lists should display for an ArtType (config.h). With
// `fallback_to_mix`, a missing variant resolves to the root mix composite
// (older libraries keep showing art); without it, `out` is left on the
// nonexistent variant path so the caller shows no art at all. Always fills
// `out`.
void ROM_displayArtPath(const char* rom_path, int art_type, bool fallback_to_mix,
						char* out, size_t out_size);
// ROM_mediaArtPath, existence-checked, with multi-disc fallback to
// <parent>/.media/<containing-folder>.png. Returns whether the path in `out` exists.
bool ROM_findArt(const char* rom_path, char* out, size_t out_size);

// Folder-named .m3u: /Roms/PSX/Game/disc1.bin -> /Roms/PSX/Game/Game.m3u.
// Writes the candidate path to m3u_path; returns whether it exists.
bool M3U_findForRom(const char* rom_path, char* m3u_path, size_t m3u_size);
// Calls fn once per existing disc (paths resolved relative to the m3u's dir),
// index counts existing discs from 0. fn returns false to stop early.
// Returns the number of discs passed to fn.
typedef bool (*M3U_DiscFn)(const char* disc_path, int index, void* ctx);
int M3U_forEachDisc(const char* m3u_path, M3U_DiscFn fn, void* ctx);

#endif
