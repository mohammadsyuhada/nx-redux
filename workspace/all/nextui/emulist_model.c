#include "emulist_model.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void Emulist_collatedPrefix(const char* path, char* out, size_t size) {
	if (!size)
		return;
	snprintf(out, size, "%s", path ? path : "");
	char* paren = strrchr(out, '(');
	if (paren)
		paren[1] = '\0';
}

bool Emulist_collates(const char* row_path, const char* folder_path) {
	if (!row_path || !folder_path)
		return false;
	// the prefix is row_path itself up to and including its last '(' -- no copy needed
	const char* paren = strrchr(row_path, '(');
	size_t len = paren ? (size_t)(paren - row_path) + 1 : strlen(row_path);
	return strncasecmp(row_path, folder_path, len) == 0; // case-insensitive like prefixMatch
}

bool Emulist_parseRow(char* line, char** path, char** name, int* count) {
	if (!line)
		return false;
	char* tab1 = strchr(line, '\t');
	if (!tab1)
		return false;
	char* tab2 = strchr(tab1 + 1, '\t');
	if (!tab2 || strchr(tab2 + 1, '\t'))
		return false; // two columns (a pre-count cache), or extra ones
	if (tab1 == line || tab2 == tab1 + 1)
		return false; // empty path or name
	const char* digits = tab2 + 1;
	if (!*digits)
		return false;
	for (const char* p = digits; *p; p++) {
		if (*p < '0' || *p > '9')
			return false; // sign, trailing junk, whitespace
	}
	errno = 0;
	long v = strtol(digits, NULL, 10);
	if (errno || v > INT_MAX)
		return false;
	*tab1 = '\0';
	*tab2 = '\0';
	*path = line;
	*name = tab1 + 1;
	*count = (int)v;
	return true;
}

int Emulist_formatRow(const char* path, const char* name, int count, char* out, size_t size) {
	return snprintf(out, size, "%s\t%s\t%d\n", path, name, count < 0 ? 0 : count);
}

void Emulist_collateCounts(const char* const* console_paths, int n, const char* const* folder_paths,
						   const int* folder_counts, int m, int* out) {
	for (int i = 0; i < n; i++) {
		int total = 0;
		for (int j = 0; j < m; j++) {
			if (Emulist_collates(console_paths[i], folder_paths[j]))
				total += folder_counts[j];
		}
		out[i] = total;
	}
}

void Emulist_adjustCount(const char* row_path, const char* removed_folder_path, int dropped, int* count) {
	if (!count || dropped <= 0 || !Emulist_collates(row_path, removed_folder_path))
		return;
	*count = *count > dropped ? *count - dropped : 0;
}
