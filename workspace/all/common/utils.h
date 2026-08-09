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

uint64_t getMicroseconds(void);

int clamp(int x, int lower, int upper);
double clampd(double x, double lower, double upper);

char* findFileInDir(const char* directory, const char* filename);

// Percent-encode src into dst (form style: space -> '+', unreserved kept, rest
// %XX). dst must hold up to 3x src plus a NUL; encoding stops early if it would
// overflow dst_size.
void urlEncode(const char* src, char* dst, size_t dst_size);

#endif
