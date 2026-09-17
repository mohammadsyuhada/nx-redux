#include "ma_cheat_match.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define CM_MAX_TOKENS 8
#define CM_TOK_LEN 48
#define CM_STEM_LEN 256

// Recognised cheat-source tags (lowercased). A parenthesised token matching
// one of these is the file's cheat SOURCE; every other token is a
// region/version QUALIFIER used for matching.
static const char* const CM_SOURCES[] = {
	"code breaker", "codebreaker", "gameshark", "game shark",
	"action replay", "pro action replay", "game genie", "gamegenie",
	"rumbles", "xploder", "game buster", "gamebuster", "raw", "wip",
	NULL};

static int cm_is_source(const char* lc) {
	for (int i = 0; CM_SOURCES[i]; i++)
		if (strcmp(lc, CM_SOURCES[i]) == 0)
			return 1;
	return 0;
}

static void cm_lower(char* s) {
	for (; *s; s++)
		*s = (char)tolower((unsigned char)*s);
}

static void cm_trim(char* s) {
	size_t n = strlen(s);
	while (n && isspace((unsigned char)s[n - 1]))
		s[--n] = '\0';
	size_t i = 0;
	while (s[i] && isspace((unsigned char)s[i]))
		i++;
	if (i)
		memmove(s, s + i, strlen(s + i) + 1);
}

// A parsed filename: stem plus classified tokens.
typedef struct {
	char stem[CM_STEM_LEN];				   // text before first " (", trimmed
	char quals[CM_MAX_TOKENS][CM_TOK_LEN]; // lowercased region/version tokens
	int nquals;
	char source[CM_TOK_LEN]; // original-case source label or ""
} CmParsed;

// Strip a trailing ".cht" (case-insensitive) if present.
static void cm_strip_cht(char* s) {
	size_t n = strlen(s);
	if (n >= 4 && strcasecmp(s + n - 4, ".cht") == 0)
		s[n - 4] = '\0';
}

// Add one comma-split token (already isolated) to the parse, classifying it.
static void cm_add_token(CmParsed* p, const char* raw) {
	char tok[CM_TOK_LEN];
	snprintf(tok, sizeof(tok), "%s", raw);
	cm_trim(tok);
	if (tok[0] == '\0')
		return;
	char lc[CM_TOK_LEN];
	snprintf(lc, sizeof(lc), "%s", tok);
	cm_lower(lc);
	if (cm_is_source(lc)) {
		if (p->source[0] == '\0')
			snprintf(p->source, sizeof(p->source), "%s", tok); // keep case
		return;
	}
	if (p->nquals < CM_MAX_TOKENS)
		snprintf(p->quals[p->nquals++], CM_TOK_LEN, "%s", lc);
}

static void cm_parse(const char* basename, CmParsed* p) {
	memset(p, 0, sizeof(*p));
	char work[CM_STEM_LEN * 2];
	snprintf(work, sizeof(work), "%s", basename);
	cm_strip_cht(work);

	// stem = up to the first " (" ; if none, the whole string
	char* paren = strstr(work, " (");
	if (paren) {
		size_t len = (size_t)(paren - work);
		if (len >= sizeof(p->stem))
			len = sizeof(p->stem) - 1;
		memcpy(p->stem, work, len);
		p->stem[len] = '\0';
	} else {
		snprintf(p->stem, sizeof(p->stem), "%s", work);
	}
	cm_trim(p->stem);

	// walk each "( ... )" group; split its contents on ", "
	const char* s = work;
	while ((s = strchr(s, '(')) != NULL) {
		const char* end = strchr(s, ')');
		if (!end)
			break;
		char group[CM_STEM_LEN];
		size_t glen = (size_t)(end - s - 1);
		if (glen >= sizeof(group))
			glen = sizeof(group) - 1;
		memcpy(group, s + 1, glen);
		group[glen] = '\0';
		// split on ", "
		char* save = group;
		char* comma;
		while ((comma = strstr(save, ", ")) != NULL) {
			*comma = '\0';
			cm_add_token(p, save);
			save = comma + 2;
		}
		cm_add_token(p, save);
		s = end + 1;
	}
}

// Count qualifier tokens of `a` that also appear in `b`.
static int cm_qual_overlap(const CmParsed* a, const CmParsed* b) {
	int c = 0;
	for (int i = 0; i < a->nquals; i++)
		for (int j = 0; j < b->nquals; j++)
			if (strcmp(a->quals[i], b->quals[j]) == 0) {
				c++;
				break;
			}
	return c;
}

// Stable sorted join of qualifier tokens, for a group key.
static void cm_group_key(const CmParsed* p, char* out, size_t out_len) {
	int order[CM_MAX_TOKENS];
	for (int i = 0; i < p->nquals; i++)
		order[i] = i;
	for (int i = 0; i < p->nquals; i++)
		for (int j = i + 1; j < p->nquals; j++)
			if (strcmp(p->quals[order[i]], p->quals[order[j]]) > 0) {
				int t = order[i];
				order[i] = order[j];
				order[j] = t;
			}
	size_t used = (size_t)snprintf(out, out_len, "%s|", p->stem);
	for (int i = 0; i < p->nquals && used < out_len; i++)
		used += (size_t)snprintf(out + used, out_len - used, "%s,", p->quals[order[i]]);
}

void CheatMatch_sourceLabel(const char* basename, char* out, size_t out_len) {
	CmParsed p;
	cm_parse(basename, &p);
	snprintf(out, out_len, "%s", p.source);
}

int CheatMatch_select(const char* rom_display, const char* rom_fullname,
					  const char* const* candidate_basenames, int n,
					  int* out_indices, int out_cap) {
	if (n <= 0 || out_cap <= 0)
		return 0;

	CmParsed rom;
	cm_parse(rom_fullname, &rom);
	// rom_display is already region-stripped by the caller; trust it as the
	// authoritative stem for the exact-match gate.
	char rom_stem[CM_STEM_LEN];
	snprintf(rom_stem, sizeof(rom_stem), "%s", rom_display);
	cm_trim(rom_stem);

	// Parse candidates; qualify those whose stem == rom_display (case-insensitive).
	CmParsed* cp = calloc((size_t)n, sizeof(CmParsed));
	if (!cp)
		return 0;
	int* qualified = calloc((size_t)n, sizeof(int));
	if (!qualified) {
		free(cp);
		return 0;
	}

	int any = 0;
	for (int i = 0; i < n; i++) {
		cm_parse(candidate_basenames[i], &cp[i]);
		qualified[i] = (strcasecmp(cp[i].stem, rom_stem) == 0);
		if (qualified[i])
			any = 1;
	}
	if (!any) {
		free(cp);
		free(qualified);
		return 0;
	}

	// Choose the winning group: max region overlap, then min extra qualifier
	// tokens (those the candidate has but the ROM lacks), then lexically
	// smallest group key for determinism.
	char best_key[CM_STEM_LEN + CM_MAX_TOKENS * (CM_TOK_LEN + 1)] = {0};
	int best_overlap = -1, best_extra = 0, have_best = 0;

	for (int i = 0; i < n; i++) {
		if (!qualified[i])
			continue;
		int overlap = cm_qual_overlap(&rom, &cp[i]);
		int extra = cp[i].nquals - overlap; // qualifier tokens not in the ROM
		char key[sizeof(best_key)];
		cm_group_key(&cp[i], key, sizeof(key));

		int better = 0;
		if (!have_best)
			better = 1;
		else if (overlap != best_overlap)
			better = (overlap > best_overlap);
		else if (extra != best_extra)
			better = (extra < best_extra);
		else
			better = (strcmp(key, best_key) < 0);

		if (better) {
			have_best = 1;
			best_overlap = overlap;
			best_extra = extra;
			snprintf(best_key, sizeof(best_key), "%s", key);
		}
	}

	// Collect all members of the winning group, ordered by source label for
	// determinism (empty source sorts first).
	int count = 0;
	for (int pass_empty = 1; pass_empty >= 0; pass_empty--) {
		for (int i = 0; i < n && count < out_cap; i++) {
			if (!qualified[i])
				continue;
			char key[sizeof(best_key)];
			cm_group_key(&cp[i], key, sizeof(key));
			if (strcmp(key, best_key) != 0)
				continue;
			int is_empty = (cp[i].source[0] == '\0');
			if (is_empty != pass_empty)
				continue;
			out_indices[count++] = i;
		}
	}

	free(cp);
	free(qualified);
	return count;
}
