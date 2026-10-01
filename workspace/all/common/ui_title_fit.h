#ifndef UI_TITLE_FIT_H
#define UI_TITLE_FIT_H

// Page-title truncation (LIST-LAYOUT §10.1). Header-only, font-agnostic and dependency-free (the caller
// supplies a width-measuring callback) so it links into nextui, every tool, minarch and the emulator
// overlay's foreign builds, and is host-tested in tests/test_title_fit.c.
//
// The title's prefix up to and including the first " | " and the optional suffix (e.g. the RA count
// " (12/39)") never truncate: only the middle part ellipsizes. When the prefix and the suffix leave room for
// fewer than UI_TITLE_MIN_MIDDLE_CPS code points of the middle, the prefix (and its " | ") is dropped and the
// middle alone is fitted before the suffix ("Astro Boy: Omega Fa… (0/66)"), so a narrow page still names its
// subject. Only a title whose middle is empty ("Consoles | ") falls back to cutting the prefix's own end. A title without " | " is a
// plain end cut before the suffix. Cuts land on UTF-8 code-point boundaries; trailing spaces (and, for a
// cut prefix, a dangling "|") before the ellipsis are dropped. A title longer than the output buffer is cut
// to fit it, still ending in the ellipsis and the suffix.

#include <stddef.h>
#include <string.h>

#define UI_TITLE_ELLIPSIS "\xE2\x80\xA6" // U+2026
#define UI_TITLE_SEPARATOR " | "
// The fewest middle code points worth keeping the protected prefix for (below this the prefix is dropped).
#define UI_TITLE_MIN_MIDDLE_CPS 8

// A fit cache's key for one input string: its full length and an FNV-1a hash of every byte, so two long
// inputs that share a fixed-size cached prefix never hit a stale fit. NULL keys like "".
typedef struct {
	size_t len;
	unsigned hash;
} UI_TitleKey;

static inline UI_TitleKey UI_titleFit_key(const char* s) {
	UI_TitleKey k = {0, 2166136261u};
	if (!s)
		return k;
	for (; s[k.len]; k.len++) {
		k.hash ^= (unsigned char)s[k.len];
		k.hash *= 16777619u;
	}
	return k;
}

static inline int UI_titleFit_keyEq(UI_TitleKey a, UI_TitleKey b) {
	return a.len == b.len && a.hash == b.hash;
}

// Returns the rendered pixel width of the NUL-terminated UTF-8 string.
typedef int (*UI_TitleMeasureFn)(const char* utf8, void* ctx);

// Append src[0..n) to out (used bytes *len, capacity size) without splitting a code point.
static inline void UI_titleFit_append(char* out, size_t size, size_t* len, const char* src, size_t n) {
	if (*len + 1 >= size)
		return;
	size_t room = size - 1 - *len;
	if (n > room) {
		n = room;
		while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
			n--;
	}
	memcpy(out + *len, src, n);
	*len += n;
	out[*len] = '\0';
}

// Step back from byte offset n of s to the previous code-point start.
static inline size_t UI_titleFit_prevCp(const char* s, size_t n) {
	if (n == 0)
		return 0;
	n--;
	while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
		n--;
	return n;
}

// Floor byte offset n of s to a code-point start.
static inline size_t UI_titleFit_floorCp(const char* s, size_t n) {
	while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
		n--;
	return n;
}

// out = head[0..head_n) + cut[0..cut_n) + ellipsis + suffix. Spaces before the ellipsis are dropped, and
// so is a dangling separator ("Consoles |") when the prefix itself is cut. Returns its width.
static inline int UI_titleFit_build(const char* head, size_t head_n, const char* cut, size_t cut_n,
									const char* ellipsis, const char* suffix, UI_TitleMeasureFn measure,
									void* ctx, char* out, size_t size) {
	size_t len = 0;
	out[0] = '\0';
	if (cut) {
		while (cut_n > 0 && cut[cut_n - 1] == ' ')
			cut_n--;
	} else {
		while (head_n > 0 && (head[head_n - 1] == ' ' || head[head_n - 1] == '|'))
			head_n--;
	}
	UI_titleFit_append(out, size, &len, head, head_n);
	if (cut)
		UI_titleFit_append(out, size, &len, cut, cut_n);
	UI_titleFit_append(out, size, &len, ellipsis, strlen(ellipsis));
	if (suffix)
		UI_titleFit_append(out, size, &len, suffix, strlen(suffix));
	return measure(out, ctx);
}

// UI_titleFit with a chosen ellipsis (NULL: UI_TITLE_ELLIPSIS), e.g. "..." for an ASCII-only bitmap font.
static inline int UI_titleFitEx(const char* title, const char* suffix, int max_w, UI_TitleMeasureFn measure,
								void* ctx, const char* ellipsis, char* out, size_t size) {
	if (!out || size == 0)
		return 0;
	out[0] = '\0';
	if (!title || !title[0] || !measure)
		return 0;
	if (!ellipsis)
		ellipsis = UI_TITLE_ELLIPSIS;
	size_t title_n = strlen(title);
	size_t suffix_n = suffix ? strlen(suffix) : 0;

	// Whole title + suffix, when it fits both the width and the buffer
	int w;
	if (title_n + suffix_n < size) {
		size_t len = 0;
		UI_titleFit_append(out, size, &len, title, title_n);
		if (suffix)
			UI_titleFit_append(out, size, &len, suffix, suffix_n);
		w = measure(out, ctx);
		if (w <= max_w)
			return w;
	}

	// Bytes left for the title's text once the ellipsis and the suffix are reserved, so a title longer
	// than the buffer still ends in the ellipsis and keeps its suffix.
	size_t tail = strlen(ellipsis) + suffix_n;
	size_t avail = size - 1 > tail ? size - 1 - tail : 0;

	// Protected prefix: through the first " | " (empty when there is none).
	const char* sep = strstr(title, UI_TITLE_SEPARATOR);
	size_t prefix_n = sep ? (size_t)(sep - title) + strlen(UI_TITLE_SEPARATOR) : 0;
	const char* middle = title + prefix_n;
	size_t middle_n = title_n - prefix_n;

	// Too narrow for the prefix plus a readable middle (fewer than UI_TITLE_MIN_MIDDLE_CPS of its code
	// points, e.g. "RetroAchievements… (0/66)" at 3x): drop the prefix and its " | " and fit the middle
	// alone, so the page still names its subject.
	if (prefix_n > 0 && middle_n > 0 && prefix_n <= avail) {
		size_t probe = 0;
		for (int i = 0; i < UI_TITLE_MIN_MIDDLE_CPS && probe < middle_n; i++) {
			probe++;
			while (probe < middle_n && ((unsigned char)middle[probe] & 0xC0) == 0x80)
				probe++;
		}
		if (probe > avail - prefix_n)
			probe = UI_titleFit_floorCp(middle, avail - prefix_n);
		w = UI_titleFit_build(title, prefix_n, middle, probe, ellipsis, suffix, measure, ctx, out, size);
		if (w > max_w) {
			// the middle + the suffix whole, when that fits the width and the buffer
			if (middle_n + suffix_n < size) {
				size_t len = 0;
				out[0] = '\0';
				UI_titleFit_append(out, size, &len, middle, middle_n);
				if (suffix)
					UI_titleFit_append(out, size, &len, suffix, suffix_n);
				w = measure(out, ctx);
				if (w <= max_w)
					return w;
			}
			size_t n = UI_titleFit_floorCp(middle, middle_n < avail ? middle_n : avail);
			for (;;) {
				w = UI_titleFit_build(middle, 0, middle, n, ellipsis, suffix, measure, ctx, out, size);
				if (w <= max_w || n == 0)
					break;
				n = UI_titleFit_prevCp(middle, n);
			}
			return w;
		}
	}

	// Cut the middle from its end, longest first.
	if (prefix_n <= avail) {
		size_t n = middle_n < avail - prefix_n ? middle_n : avail - prefix_n;
		n = UI_titleFit_floorCp(middle, n);
		for (;;) {
			w = UI_titleFit_build(title, prefix_n, middle, n, ellipsis, suffix, measure, ctx, out, size);
			if (w <= max_w || n == 0)
				break;
			n = UI_titleFit_prevCp(middle, n);
		}
		if (w <= max_w)
			return w;
	}

	// Prefix + ellipsis + suffix is still too wide: cut the prefix's own end (first just its separator).
	size_t n = UI_titleFit_floorCp(title, prefix_n < avail ? prefix_n : avail);
	for (;;) {
		w = UI_titleFit_build(title, n, NULL, 0, ellipsis, suffix, measure, ctx, out, size);
		if (w <= max_w || n == 0)
			break;
		n = UI_titleFit_prevCp(title, n);
	}
	return w;
}

// Fit title (+ optional suffix, NULL for none) into max_w px with the "…" ellipsis; writes the result into
// out (size bytes). Returns the result's measured width (0 for an empty title).
static inline int UI_titleFit(const char* title, const char* suffix, int max_w, UI_TitleMeasureFn measure,
							  void* ctx, char* out, size_t size) {
	return UI_titleFitEx(title, suffix, max_w, measure, ctx, UI_TITLE_ELLIPSIS, out, size);
}

#endif // UI_TITLE_FIT_H
