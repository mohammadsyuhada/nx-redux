#ifndef _GNU_SOURCE
#define _GNU_SOURCE // for strcasestr
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include "defines.h"
#include "utils.h"

///////////////////////////////////////

volatile bool app_quit = false;

void sig_handler(int sig) {
	if (sig == SIGINT || sig == SIGTERM)
		app_quit = true;
}

void setup_signal_handlers(void) {
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
}

///////////////////////////////////////

int prefixMatch(char* pre, const char* str) {
	return (strncasecmp(pre, str, strlen(pre)) == 0);
}
int suffixMatch(char* suf, const char* str) {
	int len = strlen(suf);
	int offset = strlen(str) - len;
	return (offset >= 0 && strncasecmp(suf, str + offset, len) == 0);
}
int exactMatch(const char* str1, const char* str2) {
	if (!str1 || !str2)
		return 0; // NULL isn't safe here
	size_t len1 = strlen(str1);
	if (len1 != strlen(str2))
		return 0;
	return (strncmp(str1, str2, len1) == 0);
}
int containsString(char* haystack, char* needle) {
	return strcasestr(haystack, needle) != NULL;
}
int hide(char* file_name) {
	return file_name[0] == '.' || suffixMatch(".disabled", file_name) || exactMatch("map.txt", file_name);
}
char* splitString(char* str, const char* delim) {
	char* p = strstr(str, delim);
	if (p == NULL)
		return NULL;		  // delimiter not found
	*p = '\0';				  // terminate string after head
	return p + strlen(delim); // return tail substring
}
void truncateString(char* string, size_t max_len) {
	if (max_len == 0)
		return; // no room even for a terminator

	size_t len = strlen(string) + 1;
	if (len <= max_len)
		return;

	if (max_len < 4) {
		// too small for an ellipsis — plain NUL truncation
		string[max_len - 1] = '\0';
		return;
	}

	strncpy(&string[max_len - 4], "...\0", 4);
}
void wrapString(char* string, size_t max_len, size_t max_lines) {
	char* line = string;

	for (size_t i = 1; i < max_lines; i++) {
		char* p = line;
		char* prev;
		do {
			prev = p;
			p = strchr(prev + 1, ' ');
		} while (p && p - line < (int)max_len);

		if (!p && strlen(line) < max_len)
			break;

		if (prev && prev != line) {
			line = prev + 1;
			*prev = '\n';
		}
	}
	truncateString(line, max_len);
}
// based on https://stackoverflow.com/a/31775567/145965
int replaceString(char* line, size_t buf_size, const char* search, const char* replace) {
	char* sp; // start of pattern
	if ((sp = strstr(line, search)) == NULL) {
		return 0;
	}
	int count = 1;
	size_t sLen = strlen(search);
	size_t rLen = strlen(replace);
	size_t tail_len = strlen(sp + sLen);
	size_t new_total = (sp - line) + rLen + tail_len + 1;
	if (new_total > buf_size) {
		return -1; // insufficient buffer space
	}
	if (sLen > rLen) {
		// move from right to left
		char* src = sp + sLen;
		char* dst = sp + rLen;
		while ((*dst = *src) != '\0') {
			dst++;
			src++;
		}
	} else if (sLen < rLen) {
		// move from left to right
		memmove(sp + rLen, sp + sLen, tail_len + 1);
	}
	memcpy(sp, replace, rLen);
	count += replaceString(sp + rLen, buf_size, search, replace);
	return count;
}
char* escapeSingleQuotes(char* str, size_t buf_size) {
	replaceString(str, buf_size, "'", "'\\''");
	return str;
}

// TODO: verify this yields the same result as the one in minui.c, remove one
// This one does not modify the input, cause we arent savages
char* replaceString2(const char* orig, char* rep, char* with) {
	const char* ins; // the next insert point
	char* tmp;		 // varies
	int len_rep;	 // length of rep (the string to remove)
	int len_with;	 // length of with (the string to replace rep with)
	int len_front;	 // distance between rep and end of last rep
	int count;		 // number of replacements

	// sanity checks and initialization
	if (!orig || !rep)
		return NULL;
	len_rep = strlen(rep);
	if (len_rep == 0)
		return NULL; // empty rep causes infinite loop during count
	if (!with)
		with = "";
	len_with = strlen(with);

	// count the number of replacements needed
	ins = orig;
	for (count = 0; (tmp = strstr(ins, rep)); ++count)
		ins = tmp + len_rep;

	char* result =
		(char*)malloc(strlen(orig) + (len_with - len_rep) * count + 1);
	tmp = result;

	if (!result)
		return NULL;

	// first time through the loop, all the variable are set correctly
	// from here on,
	//    tmp points to the end of the result string
	//    ins points to the next occurrence of rep in orig
	//    orig points to the remainder of orig after "end of rep"
	while (count--) {
		ins = strstr(orig, rep);
		len_front = ins - orig;
		tmp = strncpy(tmp, orig, len_front) + len_front;
		tmp = strcpy(tmp, with) + len_with;
		orig += len_front + len_rep; // move to next "end of rep"
	}
	strcpy(tmp, orig);
	return result;
}
// Stores the trimmed input string into the given output buffer, which must be
// large enough to store the result.  If it is too small, the output is
// truncated.
size_t trimString(char* out, size_t len, const char* str, bool first) {
	if (len == 0)
		return 0;

	const char* end;
	size_t out_size;
	bool is_string = false;

	// Trim leading space
	while (strchr("\r\n\t {},", (unsigned char)*str) != NULL)
		str++;

	end = str + 1;

	if ((unsigned char)*str == '"') {
		is_string = true;
		str++;
		while (strchr("\r\n\"", (unsigned char)*end) == NULL)
			end++;
	}

	if (*str == 0) // All spaces?
	{
		*out = 0;
		return 1;
	}

	// Trim trailing space
	if (first)
		while (strchr("\r\n\t {},", (unsigned char)*end) == NULL)
			end++;
	else {
		end = str + strlen(str) - 1;
		while (end > str && strchr("\r\n\t {},", (unsigned char)*end) != NULL)
			end--;
		end++;
	}

	if (is_string && (unsigned char)*(end - 1) == '"')
		end--;

	// Set output size to minimum of trimmed string length and buffer size minus
	// 1
	out_size = (size_t)(end - str) < len - 1 ? (size_t)(end - str) : len - 1;

	// Copy trimmed string and add null terminator
	memcpy(out, str, out_size);
	out[out_size] = 0;

	return out_size;
}

void removeParentheses(char* str_out, const char* str_in) {
	char temp[STR_MAX];
	int len = strlen(str_in);
	int c = 0;
	bool inside = false;
	char end_char;

	for (int i = 0; i < len && i < STR_MAX; i++) {
		if (!inside && (str_in[i] == '(' || str_in[i] == '[')) {
			end_char = str_in[i] == '(' ? ')' : ']';
			inside = true;
			continue;
		} else if (inside) {
			if (str_in[i] == end_char)
				inside = false;
			continue;
		}
		temp[c++] = str_in[i];
	}

	temp[c] = '\0';

	trimString(str_out, STR_MAX - 1, temp, false);
}
void serializeTime(char* dest_str, int nTime) {
	if (nTime >= 60) {
		int h = nTime / 3600;
		int m = (nTime - 3600 * h) / 60;
		if (h > 0) {
			sprintf(dest_str, "%dh %dm", h, m);
		} else {
			sprintf(dest_str, "%dm %ds", m, nTime - 60 * m);
		}
	} else {
		sprintf(dest_str, "%ds", nTime);
	}
}
void format_time(char* buf, int seconds) {
	int hrs = seconds / 3600;
	int mins = (seconds % 3600) / 60;
	int secs = seconds % 60;
	if (hrs > 0) {
		sprintf(buf, "%d:%02d:%02d", hrs, mins, secs);
	} else {
		sprintf(buf, "%02d:%02d", mins, secs);
	}
}
int countChar(const char* str, char ch) {
	size_t i;
	int count = 0;
	for (i = 0; i <= strlen(str); i++) {
		if (str[i] == ch) {
			count++;
		}
	}
	return count;
}
char* removeExtension(const char* myStr) {
	if (myStr == NULL)
		return NULL;
	char* retStr = (char*)malloc(strlen(myStr) + 1);
	char* lastExt;
	if (retStr == NULL)
		return NULL;
	strcpy(retStr, myStr);
	if ((lastExt = strrchr(retStr, '.')) != NULL && *(lastExt + 1) != ' ' && *(lastExt + 2) != '\0')
		*lastExt = '\0';
	return retStr;
}
const char* baseName(const char* filename) {
	char* p = strrchr(filename, '/');
	return p ? p + 1 : (char*)filename;
}
void folderPath(const char* path, char* result) {
	char pathCopy[MAX_PATH];
	strncpy(pathCopy, path, sizeof(pathCopy) - 1);
	pathCopy[sizeof(pathCopy) - 1] = '\0';

	char* lastSlash = strrchr(pathCopy, '/'); // Find the last slash
	if (lastSlash != NULL) {
		*lastSlash = '\0';		  // Cut off the filename
		strcpy(result, pathCopy); // Copy the remaining path
	} else {
		strcpy(result, ""); // No folder found
	}
}
void urlEncode(const char* src, char* dst, size_t dst_size) {
	const char* hex = "0123456789ABCDEF";
	size_t j = 0;
	for (size_t i = 0; src[i] && j < dst_size - 4; i++) {
		unsigned char c = (unsigned char)src[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			dst[j++] = c;
		} else if (c == ' ') {
			dst[j++] = '+';
		} else {
			dst[j++] = '%';
			dst[j++] = hex[c >> 4];
			dst[j++] = hex[c & 0x0F];
		}
	}
	dst[j] = '\0';
}
// DJB2 over the string's bytes (unsigned); used for cache filenames and hash buckets
unsigned int hashString(const char* str) {
	unsigned int h = 5381;
	for (const unsigned char* p = (const unsigned char*)str; *p; p++)
		h = h * 33 + *p;
	return h;
}
void cleanName(char* name_out, const char* file_name) {
	char* name_without_ext = removeExtension(file_name);
	char* no_underscores = replaceString2(name_without_ext, "_", " ");
	char* dot_ptr = strstr(no_underscores, ".");
	if (dot_ptr != NULL) {
		char* s = no_underscores;
		while (isdigit(*s) && s < dot_ptr)
			s++;
		if (s != dot_ptr)
			dot_ptr = no_underscores;
		else {
			dot_ptr++;
			if (dot_ptr[0] == ' ')
				dot_ptr++;
		}
	} else {
		dot_ptr = no_underscores;
	}
	removeParentheses(name_out, dot_ptr);
	free(name_without_ext);
	free(no_underscores);
}
bool pathRelativeTo(char* path_out, const char* dir_from, const char* file_to) {
	path_out[0] = '\0';

	char abs_from[MAX_PATH];
	char abs_to[MAX_PATH];
	if (realpath(dir_from, abs_from) == NULL || realpath(file_to, abs_to) == NULL) {
		return false;
	}

	char* p1 = abs_from;
	char* p2 = abs_to;
	while (*p1 && (*p1 == *p2)) {
		++p1, ++p2;
	}

	if (*p2 == '/') {
		++p2;
	}

	if (strlen(p1) > 0) {
		int num_parens = countChar(p1, '/') + 1;
		for (int i = 0; i < num_parens; i++) {
			strcat(path_out, "../");
		}
	}
	strcat(path_out, p2);

	return true;
}

void getDisplayName(const char* in_name, char* out_name) { // NOTE: out_name needs to be MAX_PATH length!
	char* tmp;
	char work_name[MAX_PATH];
	strcpy(work_name, in_name);
	strcpy(out_name, in_name);

	// extract just the filename if necessary
	tmp = strrchr(work_name, '/');
	if (tmp)
		strcpy(out_name, tmp + 1);

	// remove extension(s), eg. .p8.png
	while ((tmp = strrchr(out_name, '.')) != NULL) {
		int len = strlen(tmp);
		if (len > 2 && len <= 5)
			tmp[0] = '\0'; // 1-4 letter extension plus dot (was 1-3, extended for .doom files)
		else
			break;
	}

	// remove trailing parens (round and square)
	strcpy(work_name, out_name);
	while ((tmp = strrchr(out_name, '(')) != NULL || (tmp = strrchr(out_name, '[')) != NULL) {
		if (tmp == out_name)
			break;
		tmp[0] = '\0';
		tmp = out_name;
	}

	// make sure we haven't nuked the entire name
	if (out_name[0] == '\0')
		strcpy(out_name, work_name);

	// remove trailing whitespace
	tmp = out_name + strlen(out_name) - 1;
	while (tmp > out_name && isspace((unsigned char)*tmp))
		tmp--;
	tmp[1] = '\0';
}
void getEmuName(const char* in_name, char* out_name) { // NOTE: both char arrays need to be MAX_PATH length!
	char* tmp;
	strcpy(out_name, in_name);
	tmp = out_name;

	// printf("--------\n  in_name: %s\n",in_name); fflush(stdout);

	// extract just the Roms folder name if necessary
	if (prefixMatch(ROMS_PATH, tmp)) {
		tmp += strlen(ROMS_PATH) + 1;
		char* tmp2 = strchr(tmp, '/');
		if (tmp2)
			tmp2[0] = '\0';
		// printf("    tmp1: %s\n", tmp);
		memmove(out_name, tmp, strlen(tmp) + 1);
		tmp = out_name;
	}

	// finally extract pak name from parenths if present
	tmp = strrchr(tmp, '(');
	if (tmp) {
		tmp += 1;
		// printf("    tmp2: %s\n", tmp);
		memmove(out_name, tmp, strlen(tmp) + 1);
		tmp = strchr(out_name, ')');
		if (tmp) // guard: an unbalanced '(' (no matching ')') would make this NULL
			tmp[0] = '\0';
	}

	// printf(" out_name: %s\n", out_name); fflush(stdout);
}
void getEmuPath(char* emu_name, char* pak_path) {
	sprintf(pak_path, "%s/Emus/%s.pak/launch.sh", SDCARD_PATH, emu_name);
	if (exists(pak_path))
		return;
	// community paks follow the MinUI convention of a platform subfolder
	// (e.g. Emus/tg5040/PSP.pak) and hardcode that path internally
	sprintf(pak_path, "%s/Emus/" PLATFORM "/%s.pak/launch.sh", SDCARD_PATH, emu_name);
	if (exists(pak_path))
		return;
	sprintf(pak_path, "%s/Emus/%s.pak/launch.sh", PAKS_PATH, emu_name);
}

void normalizeNewline(char* line) {
	int len = strlen(line);
	if (len > 1 && line[len - 1] == '\n' && line[len - 2] == '\r') { // windows!
		line[len - 2] = '\n';
		line[len - 1] = '\0';
	}
}
void trimTrailingNewlines(char* line) {
	int len = strlen(line);
	while (len > 0 && line[len - 1] == '\n') {
		line[len - 1] = '\0'; // trim newline
		len -= 1;
	}
}
void trimSortingMeta(char** str) { // eg. `001) `
	// TODO: this code is suss
	char* safe = *str;
	while (isdigit(**str))
		*str += 1; // ignore leading numbers

	if (*str[0] == ')') { // then match a closing parenthesis
		*str += 1;
	} else { //  or bail, restoring the string to its original value
		*str = safe;
		return;
	}

	while (isblank(**str))
		*str += 1; // ignore leading space
}

///////////////////////////////////////

void mkdir_p(const char* path) {
	char tmp[MAX_PATH];
	strncpy(tmp, path, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = '\0';
	for (char* p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0777);
			*p = '/';
		}
	}
	mkdir(tmp, 0777);
}

bool exists(char* path) {
	return access(path, F_OK) == 0;
}
void touch(char* path) {
	close(open(path, O_RDWR | O_CREAT, 0777));
}
int toggle(char* path) {
	if (access(path, F_OK) == 0) {
		unlink(path);
		return 0;
	} else {
		touch(path);
		return 1;
	}
}
void putFile(char* path, char* contents) {
	FILE* file = fopen(path, "w");
	if (file) {
		fputs(contents, file);
		fclose(file);
	}
}
int writeFileAtomic(const char* path, const char* data, size_t len) {
	// stage to a sibling tmp then rename so a power cut or ENOSPC mid-write
	// can never leave a truncated file at path; rename also refreshes the
	// mtime that cache-staleness checks rely on
	char tmp[MAX_PATH + 16];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* file = fopen(tmp, "wb");
	if (!file)
		return 0;
	size_t wr = fwrite(data, 1, len, file);
	fflush(file);
	fsync(fileno(file));
	fclose(file);
	if (wr != len || rename(tmp, path) != 0) {
		remove(tmp);
		return 0;
	}
	return 1;
}
void getFile(char* path, char* buffer, size_t buffer_size) {
	if (buffer_size == 0)
		return; // no room even for a terminator
	FILE* file = fopen(path, "r");
	if (file) {
		fseek(file, 0L, SEEK_END);
		long ftell_size = ftell(file);
		// ftell can fail (unseekable file / error) — treat as empty
		size_t size = ftell_size < 0 ? 0 : (size_t)ftell_size;
		if (size > buffer_size - 1)
			size = buffer_size - 1;
		rewind(file);
		fread(buffer, sizeof(char), size, file);
		fclose(file);
		buffer[size] = '\0';
	}
}
char* allocFile(char* path) { // caller must free!
	char* contents = NULL;
	FILE* file = fopen(path, "r");
	if (file) {
		fseek(file, 0L, SEEK_END);
		long size = ftell(file);
		if (size < 0) { // unseekable file / ftell error — fail cleanly
			fclose(file);
			return NULL;
		}
		contents = calloc(size + 1, sizeof(char));
		if (contents) {
			fseek(file, 0L, SEEK_SET);
			fread(contents, sizeof(char), size, file);
			contents[size] = '\0';
		}
		fclose(file);
	}
	return contents;
}
bool SimpleMode_readPin(char pin_out[5]) {
	char buf[16] = {0};
	getFile((char*)SIMPLE_MODE_PATH, buf, sizeof(buf));
	int len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
					   buf[len - 1] == ' ' || buf[len - 1] == '\t'))
		buf[--len] = '\0';
	if (len != 4)
		return false;
	for (int i = 0; i < 4; i++)
		if (buf[i] < '0' || buf[i] > '9')
			return false;
	if (pin_out) {
		memcpy(pin_out, buf, 4);
		pin_out[4] = '\0';
	}
	return true;
}
int getInt(char* path) {
	int i = 0;
	if (path == NULL)
		return i;

	FILE* file = fopen(path, "r");
	if (file != NULL) {
		int res = fscanf(file, "%i", &i);
		fclose(file);
		if (res != 1)
			i = 0; // failed to parse int
	}
	return i;
}
void putInt(char* path, int value) {
	char buffer[8];
	sprintf(buffer, "%d", value);
	putFile(path, buffer);
}

int run_cmd_capture(const char* cmd, char* output, size_t output_len) {
	FILE* fp = popen(cmd, "r");
	if (!fp) {
		return -1;
	}

	if (output && output_len > 0) {
		output[0] = '\0';
		size_t total = 0;
		char buf[256];
		while (fgets(buf, sizeof(buf), fp) && total < output_len - 1) {
			size_t len = strlen(buf);
			if (total + len >= output_len) {
				len = output_len - total - 1;
			}
			memcpy(output + total, buf, len);
			total += len;
		}
		output[total] = '\0';
	}

	int status = pclose(fp);
	return WEXITSTATUS(status);
}

uint64_t getMicroseconds(void) {
	uint64_t ret;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	ret = (uint64_t)tv.tv_sec * 1000000;
	ret += (uint64_t)tv.tv_usec;

	return ret;
}

#define max(a, b)               \
	({                          \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _a : _b;      \
	})

#define min(a, b)               \
	({                          \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a < _b ? _a : _b;      \
	})

int clamp(int x, int lower, int upper) {
	return min(upper, max(x, lower));
}

double clampd(double x, double lower, double upper) {
	return min(upper, max(x, lower));
}

char* findFileInDir(const char* directory, const char* filename) {
	char* filename_copy = strdup(filename);
	if (!filename_copy) {
		perror("strdup");
		return NULL;
	}

	// Strip extension from filename
	char* dot_pos = strrchr(filename_copy, '.');
	if (dot_pos) {
		*dot_pos = '\0';
	}

	DIR* dir = opendir(directory);
	if (!dir) {
		perror("opendir");
		free(filename_copy);
		return NULL;
	}

	struct dirent* entry;
	char* full_path = NULL;

	// Track the best (shortest) match to avoid prefix collisions.
	// e.g., searching for "Advance Wars" should match "Advance Wars (USA).gba"
	// over "Advance Wars 2 - Black Hole Rising (USA).gba"
	char* best_match_name = NULL;
	size_t best_match_len = SIZE_MAX;

	while ((entry = readdir(dir)) != NULL) {
		// Strip extension from entry for comparison
		char* entry_base = strdup(entry->d_name);
		if (!entry_base)
			continue;

		char* entry_dot = strrchr(entry_base, '.');
		if (entry_dot)
			*entry_dot = '\0';

		if (strstr(entry_base, filename_copy) == entry_base) {
			// Prefer shorter matches (closer to exact match)
			size_t entry_len = strlen(entry_base);
			if (entry_len < best_match_len) {
				free(best_match_name);
				best_match_name = strdup(entry->d_name);
				best_match_len = entry_len;
			}
		}
		free(entry_base);
	}

	closedir(dir);

	if (best_match_name) {
		full_path = (char*)malloc(strlen(directory) + strlen(best_match_name) + 2);
		if (full_path) {
			snprintf(full_path, strlen(directory) + strlen(best_match_name) + 2, "%s/%s", directory, best_match_name);
		}
		free(best_match_name);
	}

	free(filename_copy);
	return full_path;
}

const char* json_extract_string(const char* json, const char* key, char* out, size_t out_size) {
	if (!json || !key || !out || out_size == 0)
		return NULL;

	// Search for "key":"value" pattern
	char search[128];
	snprintf(search, sizeof(search), "\"%s\":\"", key);

	const char* start = strstr(json, search);
	if (!start) {
		// Try "key": "value" (with space)
		snprintf(search, sizeof(search), "\"%s\": \"", key);
		start = strstr(json, search);
		if (!start)
			return NULL;
	}

	start += strlen(search);
	const char* end = strchr(start, '"');
	if (!end)
		return NULL;

	size_t len = end - start;
	if (len >= out_size)
		len = out_size - 1;

	strncpy(out, start, len);
	out[len] = '\0';

	return out;
}

void ROM_mediaArtPath(const char* rom_path, char* out, size_t out_size) {
	char dir[MAX_PATH];
	char base[MAX_PATH];
	strncpy(dir, rom_path, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';
	char* slash = strrchr(dir, '/');
	if (slash)
		*slash = '\0';
	else
		strcpy(dir, ".");
	const char* bn = strrchr(rom_path, '/');
	bn = bn ? bn + 1 : rom_path;
	strncpy(base, bn, sizeof(base) - 1);
	base[sizeof(base) - 1] = '\0';
	char* dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';
	snprintf(out, out_size, "%s/.media/%s.png", dir, base);
}

// ROM_mediaArtPath with the variant subfolder spliced in:
// /Roms/GBA/Game.gba + "screenshot" -> /Roms/GBA/.media/screenshot/Game.png.
// A NULL or empty variant yields the root mix path.
void ROM_mediaArtVariantPath(const char* rom_path, const char* variant,
							 char* out, size_t out_size) {
	ROM_mediaArtPath(rom_path, out, out_size);
	if (!variant || !variant[0])
		return;

	char base_path[MAX_PATH];
	strncpy(base_path, out, sizeof(base_path) - 1);
	base_path[sizeof(base_path) - 1] = '\0';
	char* slash = strrchr(base_path, '/');
	if (!slash)
		return; // no .media component to splice into
	*slash = '\0';
	snprintf(out, out_size, "%s/%s/%s", base_path, variant, slash + 1);
}

// Maps an ArtType (config.h: 0 mix, 1 screenshot, 2 box art) to its variant
// folder. Kept int-typed so utils.c stays free of config.h.
static const char* artVariantFolder(int art_type) {
	switch (art_type) {
	case 1:
		return "screenshot";
	case 2:
		return "boxart";
	default:
		return NULL;
	}
}

void ROM_displayArtPath(const char* rom_path, int art_type, bool fallback_to_mix,
						char* out, size_t out_size) {
	const char* variant = artVariantFolder(art_type);
	if (variant) {
		ROM_mediaArtVariantPath(rom_path, variant, out, out_size);
		// Caller asked for this variant only: leave the missing path in place
		// so nothing is drawn, rather than substituting a different image.
		if (!fallback_to_mix || exists(out))
			return;
	}
	// No variant requested, or the scraper never wrote one for this game
	// (older libraries only have the mix composite).
	ROM_mediaArtPath(rom_path, out, out_size);
}

bool ROM_findArt(const char* rom_path, char* out, size_t out_size) {
	ROM_mediaArtPath(rom_path, out, out_size);
	if (exists(out))
		return true;
	// multi-disc folder games: art named after the containing folder, one level up
	char dir[MAX_PATH];
	strncpy(dir, rom_path, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';
	char* slash = strrchr(dir, '/');
	if (!slash)
		return false;
	*slash = '\0';
	char* parent_slash = strrchr(dir, '/');
	if (!parent_slash || parent_slash == dir)
		return false;
	*parent_slash = '\0';
	snprintf(out, out_size, "%s/.media/%s.png", dir, parent_slash + 1);
	return exists(out);
}

bool M3U_findForRom(const char* rom_path, char* m3u_path, size_t m3u_size) {
	char work[MAX_PATH];
	strncpy(work, rom_path, sizeof(work) - 1);
	work[sizeof(work) - 1] = '\0';
	char* tmp = strrchr(work, '/');
	if (!tmp)
		return false;
	tmp[0] = '\0';
	char* dir_name = strrchr(work, '/');
	if (!dir_name)
		return false;
	dir_name++;
	snprintf(m3u_path, m3u_size, "%s/%s.m3u", work, dir_name);
	return exists(m3u_path);
}

int M3U_forEachDisc(const char* m3u_path, M3U_DiscFn fn, void* ctx) {
	char base_path[MAX_PATH];
	strncpy(base_path, m3u_path, sizeof(base_path) - 1);
	base_path[sizeof(base_path) - 1] = '\0';
	char* slash = strrchr(base_path, '/');
	if (!slash)
		return 0;
	slash[1] = '\0';

	FILE* file = fopen(m3u_path, "r");
	if (!file)
		return 0;
	int count = 0;
	char line[MAX_PATH];
	while (fgets(line, sizeof(line), file) != NULL) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		if (strlen(line) == 0)
			continue;
		char disc_path[MAX_PATH];
		snprintf(disc_path, sizeof(disc_path), "%s%s", base_path, line);
		if (!exists(disc_path))
			continue;
		bool keep = fn(disc_path, count, ctx);
		count++;
		if (!keep)
			break;
	}
	fclose(file);
	return count;
}
