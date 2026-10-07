// Runtime font cache: the system UI font at the sizes callers ask for (see ui_font.h). UI thread only: no locking.

#include "ui_font.h"

#include <stdio.h>

#include "api.h"
#include "defines.h"

// SDL_ttf on tg5040 is 2.0.13 (no TTF_SetFontSize, which arrived in 2.0.18; tg5050 ships 2.0.18), so each
// size stays its own TTF_Font. 64 holds the whole-sp working set of the compose paths (Grid/Carousel/Home
// shrink loops at any UI scale) without evicting; each open face costs little memory (glyphs load lazily).
#define UIFONT_MAX 64

typedef struct {
	int px;
	bool bold;
	TTF_Font* f;
	TTF_Font* ar;  // Arabic counterpart (font1-arabic.ttf, same px + style), opened on first Arabic text
	bool ar_tried; // the Arabic open was attempted (don't retry a missing file every frame)
	unsigned lru;  // last use, for eviction
} UIFontEntry;

static UIFontEntry cache[UIFONT_MAX];
static int cache_count = 0;
static unsigned cache_tick = 0;

// Debug counter: TTF_OpenFont calls this process (primary + Arabic). A steadily rising count while scrolling means
// the working set outgrows UIFONT_MAX (each open parses the 8 MB CJK font1.ttf). Logged every 50 opens and when
// the cache closes (font reload, GFX_quit).
static unsigned open_count = 0;
static unsigned open_logged = 0;

static void countOpen(int px) {
	if (++open_count % 50 == 0) {
		LOG_info("UIFont: %u font opens so far (latest %dpx, %d cached)\n", open_count, px, cache_count);
		open_logged = open_count;
	}
}

// Close a cached font, dropping its GFX_getCachedText surfaces first (that cache keys on the pointer).
static void closeFont(TTF_Font* f) {
	if (!f)
		return;
	GFX_forgetFontText(f);
	TTF_CloseFont(f);
}

// Close an entry: the primary (whose key also covers its Arabic-rendered text) and its Arabic counterpart.
static void closeEntry(UIFontEntry* e) {
	closeFont(e->f);
	closeFont(e->ar);
	e->f = NULL;
	e->ar = NULL;
}

void UIFont_quit(void) {
	if (open_count != open_logged) {
		LOG_info("UIFont: %u font opens so far\n", open_count);
		open_logged = open_count;
	}
	for (int i = 0; i < cache_count; i++)
		closeEntry(&cache[i]);
	cache_count = 0;
}

// GFX_fallbackFontFor resolver: the Arabic face for one of our fonts (NULL for any other font, or when
// font1-arabic.ttf can't be opened, in which case Arabic falls back to the primary as before). UI thread only,
// like the whole cache: it reads and opens entries without a lock.
static TTF_Font* arabicFor(TTF_Font* primary) {
	for (int i = 0; i < cache_count; i++) {
		UIFontEntry* e = &cache[i];
		if (e->f != primary)
			continue;
		if (!e->ar && !e->ar_tried) {
			e->ar_tried = true;
			const char* path = GFX_getArabicFontPath();
			e->ar = TTF_OpenFont(path, e->px);
			countOpen(e->px);
			if (!e->ar)
				LOG_warn("UIFont: can't open %s at %dpx: %s\n", path, e->px, TTF_GetError());
		}
		return e->ar;
	}
	return NULL;
}

TTF_Font* UIFont_get(float sp, bool bold) {
	return UIFont_getPx(NX_SP(sp), bold);
}

TTF_Font* UIFont_getPx(int px, bool bold) {
	if (px < 1)
		px = 1;
	for (int i = 0; i < cache_count; i++)
		if (cache[i].px == px && cache[i].bold == bold) {
			cache[i].lru = ++cache_tick;
			return cache[i].f;
		}
	if (cache_count >= UIFONT_MAX) {
		// Full: close the least recently used (and its cached text). A font pointer is only valid until a
		// later UIFont_get call evicts it: use it before the next UIFont_get, and don't keep it.
		int victim = 0;
		for (int i = 1; i < cache_count; i++)
			if (cache[i].lru < cache[victim].lru)
				victim = i;
		closeEntry(&cache[victim]);
		cache[victim] = cache[--cache_count];
	}
	if (!TTF_WasInit())
		TTF_Init();
	// Close the cache whenever the system fonts reload (font change) and at GFX_quit, and
	// give our fonts their Arabic counterparts in GFX_renderText/GFX_measureText/GFX_getCachedText.
	GFX_setFontReloadHook(UIFont_quit);
	GFX_setFallbackFontResolver(arabicFor);
	const char* path = GFX_getSystemFontPath();
	TTF_Font* f = TTF_OpenFont(path, px);
	countOpen(px);
	if (!f) {
		LOG_warn("UIFont_get: can't open %s at %dpx: %s\n", path, px, TTF_GetError());
		return NULL;
	}
	// `bold` is kept in the key but draws the regular face: the UI font ships one weight, and TTF_STYLE_BOLD's
	// synthetic emboldening reads heavy with rough edges
	cache[cache_count++] = (UIFontEntry){px, bold, f, NULL, false, ++cache_tick};
	return f;
}

// Next codepoint of a UTF-8 string, advancing *s. A stray or truncated sequence reads as U+FFFD and
// consumes only the bytes it matched.
static Uint32 nextCodepoint(const unsigned char** s) {
	const unsigned char* p = *s;
	Uint32 cp;
	int extra;
	if (p[0] < 0x80) {
		*s = p + 1;
		return p[0];
	} else if ((p[0] & 0xE0) == 0xC0) {
		cp = p[0] & 0x1F;
		extra = 1;
	} else if ((p[0] & 0xF0) == 0xE0) {
		cp = p[0] & 0x0F;
		extra = 2;
	} else if ((p[0] & 0xF8) == 0xF0) {
		cp = p[0] & 0x07;
		extra = 3;
	} else {
		*s = p + 1;
		return 0xFFFD;
	}
	for (int i = 1; i <= extra; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			*s = p + i;
			return 0xFFFD;
		}
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	*s = p + extra + 1;
	return cp;
}

static int glyphMetrics(TTF_Font* f, Uint32 cp, int* miny, int* maxy) {
	int minx, maxx, adv;
#if SDL_TTF_MAJOR_VERSION > 2 || (SDL_TTF_MAJOR_VERSION == 2 && (SDL_TTF_MINOR_VERSION > 0 || SDL_TTF_PATCHLEVEL >= 18))
	return TTF_GlyphMetrics32(f, cp, &minx, &maxx, miny, maxy, &adv);
#else
	if (cp > 0xFFFF) // SDL_ttf < 2.0.18 only takes UCS-2
		return -1;
	return TTF_GlyphMetrics(f, (Uint16)cp, &minx, &maxx, miny, maxy, &adv);
#endif
}

void UIFont_inkBounds(TTF_Font* f, const char* text, int* top, int* bottom) {
	int t = 0, b = 0;
	bool any = false;
	if (f && text) {
		const unsigned char* s = (const unsigned char*)text;
		while (*s) {
			Uint32 cp = nextCodepoint(&s);
			int miny, maxy;
			if (glyphMetrics(f, cp, &miny, &maxy) != 0)
				continue;
			if (!any || maxy > t)
				t = maxy;
			if (!any || miny < b)
				b = miny;
			any = true;
		}
	}
	if (top)
		*top = t;
	if (bottom)
		*bottom = b;
}

// ---- Text roles (LIST-LAYOUT §10.6) ----

float UI_textRoleLogical(UITextRole role) {
	switch (role) {
	case UI_TEXT_HEADLINE:
		return UI_TEXT_LABEL_LOGICAL * 1.15f;
	case UI_TEXT_SECONDARY:
		return UI_TEXT_LABEL_LOGICAL * 0.8f;
	case UI_TEXT_CAPTION:
		return UI_TEXT_LABEL_LOGICAL * 0.64f;
	case UI_TEXT_LABEL:
	default:
		return UI_TEXT_LABEL_LOGICAL;
	}
}

int UI_textRolePx(UITextRole role) {
	return (int)(UI_textRoleLogical(role) * FIXED_SCALE + 0.5f);
}

TTF_Font* UI_textRole(UITextRole role, bool bold) {
	// the label is font.large itself (shares its text cache); the other roles open the system font at their px
	if (role == UI_TEXT_LABEL && !bold && font.large)
		return font.large;
	return UIFont_getPx(UI_textRolePx(role), bold);
}
