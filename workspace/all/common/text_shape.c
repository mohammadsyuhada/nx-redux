#include "text_shape.h"
#include <stdint.h>
#include <string.h>

#define TS_MAX 512 // max codepoints per string (UI labels/names are short)

// ---------------------------------------------------------------------------
// UTF-8 codec
// ---------------------------------------------------------------------------

// Decode one codepoint from s; returns bytes consumed (>=1) and sets *cp.
// Invalid sequences yield U+FFFD and consume one byte.
static int utf8_decode(const unsigned char* s, uint32_t* cp) {
	unsigned char c = s[0];
	if (c < 0x80) {
		*cp = c;
		return 1;
	}
	if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
		*cp = ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3F);
		return 2;
	}
	if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
		*cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
		return 3;
	}
	if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
		(s[3] & 0xC0) == 0x80) {
		*cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
			  ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
		return 4;
	}
	*cp = 0xFFFD;
	return 1;
}

// Encode cp into buf (<= 4 bytes); returns bytes written.
static int utf8_encode(uint32_t cp, char* buf) {
	if (cp < 0x80) {
		buf[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		buf[0] = (char)(0xC0 | (cp >> 6));
		buf[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		buf[0] = (char)(0xE0 | (cp >> 12));
		buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		buf[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	buf[0] = (char)(0xF0 | (cp >> 18));
	buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	buf[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

// ---------------------------------------------------------------------------
// Arabic classification + shaping tables (Unicode-derived; see
// docs/superpowers/specs/arabic-shaping-table.txt)
// ---------------------------------------------------------------------------

static bool is_arabic_cp(uint32_t cp) {
	return (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
		   (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
		   (cp >= 0xFE70 && cp <= 0xFEFF);
}

// Joining types.
enum { JT_U = 0,
	   JT_D,
	   JT_R,
	   JT_C,
	   JT_T };

// Transparent = combining marks that sit on a base and don't affect joining.
static bool is_transparent(uint32_t cp) {
	return (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 ||
		   (cp >= 0x06D6 && cp <= 0x06DC) || (cp >= 0x06DF && cp <= 0x06E4) ||
		   cp == 0x06E7 || cp == 0x06E8 || (cp >= 0x06EA && cp <= 0x06ED);
}

// Contextual forms for the standard Arabic letters U+0621..U+064A.
// {iso, fin, ini, med} presentation codepoints (0 = form absent); cls D/R/U.
typedef struct {
	uint16_t iso, fin, ini, med;
	uint8_t cls; // JT_D, JT_R, or JT_U (non-joining, e.g. hamza / gaps)
} ArShape;

#define AR_BASE 0x0621
#define AR_LAST 0x064A
static const ArShape AR[AR_LAST - AR_BASE + 1] = {
	/*0621*/ {0xFE80, 0, 0, 0, JT_U},
	/*0622*/ {0xFE81, 0xFE82, 0, 0, JT_R},
	/*0623*/ {0xFE83, 0xFE84, 0, 0, JT_R},
	/*0624*/ {0xFE85, 0xFE86, 0, 0, JT_R},
	/*0625*/ {0xFE87, 0xFE88, 0, 0, JT_R},
	/*0626*/ {0xFE89, 0xFE8A, 0xFE8B, 0xFE8C, JT_D},
	/*0627*/ {0xFE8D, 0xFE8E, 0, 0, JT_R},
	/*0628*/ {0xFE8F, 0xFE90, 0xFE91, 0xFE92, JT_D},
	/*0629*/ {0xFE93, 0xFE94, 0, 0, JT_R},
	/*062A*/ {0xFE95, 0xFE96, 0xFE97, 0xFE98, JT_D},
	/*062B*/ {0xFE99, 0xFE9A, 0xFE9B, 0xFE9C, JT_D},
	/*062C*/ {0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0, JT_D},
	/*062D*/ {0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4, JT_D},
	/*062E*/ {0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8, JT_D},
	/*062F*/ {0xFEA9, 0xFEAA, 0, 0, JT_R},
	/*0630*/ {0xFEAB, 0xFEAC, 0, 0, JT_R},
	/*0631*/ {0xFEAD, 0xFEAE, 0, 0, JT_R},
	/*0632*/ {0xFEAF, 0xFEB0, 0, 0, JT_R},
	/*0633*/ {0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4, JT_D},
	/*0634*/ {0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8, JT_D},
	/*0635*/ {0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC, JT_D},
	/*0636*/ {0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0, JT_D},
	/*0637*/ {0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4, JT_D},
	/*0638*/ {0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8, JT_D},
	/*0639*/ {0xFEC9, 0xFECA, 0xFECB, 0xFECC, JT_D},
	/*063A*/ {0xFECD, 0xFECE, 0xFECF, 0xFED0, JT_D},
	/*063B*/ {0, 0, 0, 0, JT_U},
	/*063C*/ {0, 0, 0, 0, JT_U},
	/*063D*/ {0, 0, 0, 0, JT_U},
	/*063E*/ {0, 0, 0, 0, JT_U},
	/*063F*/ {0, 0, 0, 0, JT_U},
	/*0640*/ {0, 0, 0, 0, JT_C}, // tatweel (kashida) — join-causing
	/*0641*/ {0xFED1, 0xFED2, 0xFED3, 0xFED4, JT_D},
	/*0642*/ {0xFED5, 0xFED6, 0xFED7, 0xFED8, JT_D},
	/*0643*/ {0xFED9, 0xFEDA, 0xFEDB, 0xFEDC, JT_D},
	/*0644*/ {0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0, JT_D},
	/*0645*/ {0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4, JT_D},
	/*0646*/ {0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8, JT_D},
	/*0647*/ {0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC, JT_D},
	/*0648*/ {0xFEED, 0xFEEE, 0, 0, JT_R},
	/*0649*/ {0xFEEF, 0xFEF0, 0, 0, JT_R},
	/*064A*/ {0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4, JT_D},
};

static const ArShape* ar_entry(uint32_t cp) {
	if (cp < AR_BASE || cp > AR_LAST)
		return NULL;
	const ArShape* e = &AR[cp - AR_BASE];
	return (e->cls == JT_D || e->cls == JT_R) ? e : NULL;
}

static int join_type(uint32_t cp) {
	if (is_transparent(cp))
		return JT_T;
	if (cp == 0x0640)
		return JT_C;
	const ArShape* e = ar_entry(cp);
	if (e)
		return e->cls;
	return JT_U;
}

// Lam-Alef ligature for lam(0644)+alef variant. prev_joins => final-form
// ligature (lam was medial), else isolated-form (lam was initial). 0 if not a
// lam-alef pair.
static uint32_t lam_alef(uint32_t alef, bool prev_joins) {
	switch (alef) {
	case 0x0622:
		return prev_joins ? 0xFEF6 : 0xFEF5;
	case 0x0623:
		return prev_joins ? 0xFEF8 : 0xFEF7;
	case 0x0625:
		return prev_joins ? 0xFEFA : 0xFEF9;
	case 0x0627:
		return prev_joins ? 0xFEFC : 0xFEFB;
	default:
		return 0;
	}
}

// ---------------------------------------------------------------------------
// Reshaping: logical codepoints -> shaped codepoints (still logical order)
// ---------------------------------------------------------------------------

// Nearest non-transparent join type before / after index i (JT_U past the ends).
static int prev_jt(const uint32_t* cp, int i) {
	for (int k = i - 1; k >= 0; k--)
		if (join_type(cp[k]) != JT_T)
			return join_type(cp[k]);
	return JT_U;
}
static int next_jt(const uint32_t* cp, int n, int i) {
	for (int k = i + 1; k < n; k++)
		if (join_type(cp[k]) != JT_T)
			return join_type(cp[k]);
	return JT_U;
}

static int reshape(const uint32_t* in, int n, uint32_t* out) {
	int m = 0;
	for (int i = 0; i < n; i++) {
		uint32_t c = in[i];
		const ArShape* e = ar_entry(c);
		if (!e) {
			out[m++] = c; // harakat, hamza, extended, non-Arabic: passthrough
			continue;
		}
		int pj = prev_jt(in, i);
		int nj = next_jt(in, n, i);
		bool joins_prev = (pj == JT_D || pj == JT_C) && (e->cls == JT_D || e->cls == JT_R);
		bool joins_next = (nj == JT_D || nj == JT_C || nj == JT_R) && (e->cls == JT_D);

		// Lam-Alef ligature (immediate alef only — keeps the common "لا" case
		// simple; a diacritic between lam and alef falls back to two glyphs).
		if (c == 0x0644 && joins_next && i + 1 < n) {
			uint32_t lig = lam_alef(in[i + 1], joins_prev);
			if (lig) {
				out[m++] = lig;
				i++; // consume the alef
				continue;
			}
		}

		uint32_t form;
		if (joins_prev)
			form = joins_next ? e->med : e->fin;
		else
			form = joins_next ? e->ini : e->iso;
		if (form == 0)
			form = e->iso; // right-joining letters in ini/med position -> iso/fin
		out[m++] = form;
	}
	return m;
}

// ---------------------------------------------------------------------------
// Pragmatic BiDi reordering (logical -> visual). Not full UBA: single
// paragraph, one embedding level, no explicit controls — sufficient for UI
// labels and station names (Arabic, optionally with embedded Latin/numbers).
// ---------------------------------------------------------------------------

enum { D_L,
	   D_R,
	   D_EN,
	   D_N };

static int dir_class(uint32_t cp) {
	if (is_arabic_cp(cp))
		return D_R;
	if (cp >= '0' && cp <= '9')
		return D_EN;
	if (cp == ' ' || cp == '\t' || cp == '\n' || (cp >= 0x21 && cp <= 0x2F) ||
		(cp >= 0x3A && cp <= 0x40) || (cp >= 0x5B && cp <= 0x60) ||
		(cp >= 0x7B && cp <= 0x7E))
		return D_N;
	return D_L; // Latin, CJK, other strong-LTR
}

// A cluster = a base codepoint plus any following transparent marks; BiDi
// reorders clusters (marks stay attached to their base in the output).
typedef struct {
	int start, len; // range into the shaped array
	int dir;		// D_L / D_R / D_EN / D_N (from the base)
	int level;
} Cluster;

static int bidi_reorder(const uint32_t* sh, int m, uint32_t* out) {
	Cluster cl[TS_MAX];
	int nc = 0;
	for (int i = 0; i < m; i++) {
		if (is_transparent(sh[i]) && nc > 0) {
			cl[nc - 1].len++;
		} else {
			cl[nc].start = i;
			cl[nc].len = 1;
			cl[nc].dir = dir_class(sh[i]);
			cl[nc].level = 0;
			nc++;
		}
	}
	if (nc == 0) {
		return 0;
	}

	// Base paragraph level from the first strong cluster (L or R).
	int base = 0;
	for (int c = 0; c < nc; c++) {
		if (cl[c].dir == D_L) {
			base = 0;
			break;
		}
		if (cl[c].dir == D_R) {
			base = 1;
			break;
		}
	}
	int L_level = (base % 2 == 0) ? base : base + 1; // LTR island level
	int R_level = (base % 2 == 0) ? base + 1 : base; // RTL run level

	for (int c = 0; c < nc; c++) {
		switch (cl[c].dir) {
		case D_L:
		case D_EN:
			cl[c].level = L_level;
			break;
		case D_R:
			cl[c].level = R_level;
			break;
		default:
			cl[c].level = -1;
			break; // neutral: resolved below
		}
	}
	// Resolve neutral clusters: take the surrounding direction if both sides
	// agree (EN counts as LTR), else the base direction.
	for (int c = 0; c < nc; c++) {
		if (cl[c].level != -1)
			continue;
		int pd = -1, nd = -1;
		for (int k = c - 1; k >= 0; k--)
			if (cl[k].dir != D_N) {
				pd = (cl[k].dir == D_R) ? D_R : D_L;
				break;
			}
		for (int k = c + 1; k < nc; k++)
			if (cl[k].dir != D_N) {
				nd = (cl[k].dir == D_R) ? D_R : D_L;
				break;
			}
		int d = (pd != -1 && pd == nd) ? pd : (base ? D_R : D_L);
		cl[c].level = (d == D_R) ? R_level : L_level;
	}

	// UBA rule L2: from the highest level down to the lowest odd level, reverse
	// each contiguous run of clusters at that level or above.
	int maxlvl = 0;
	for (int c = 0; c < nc; c++)
		if (cl[c].level > maxlvl)
			maxlvl = cl[c].level;
	for (int lv = maxlvl; lv >= 1; lv--) {
		int c = 0;
		while (c < nc) {
			if (cl[c].level >= lv) {
				int j = c;
				while (j < nc && cl[j].level >= lv)
					j++;
				for (int a = c, b = j - 1; a < b; a++, b--) {
					Cluster t = cl[a];
					cl[a] = cl[b];
					cl[b] = t;
				}
				c = j;
			} else {
				c++;
			}
		}
	}

	// Emit clusters in visual order, each cluster's codepoints in base->mark
	// order (so combining marks still follow their base).
	int o = 0;
	for (int c = 0; c < nc; c++)
		for (int k = 0; k < cl[c].len; k++)
			out[o++] = sh[cl[c].start + k];
	return o;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool TextShape_baseIsRTL(const char* utf8) {
	if (!utf8)
		return false;
	const unsigned char* s = (const unsigned char*)utf8;
	while (*s) {
		uint32_t cp;
		s += utf8_decode(s, &cp);
		int d = dir_class(cp);
		if (d == D_R)
			return true;
		if (d == D_L)
			return false;
	}
	return false;
}

bool TextShape_hasArabic(const char* utf8) {
	if (!utf8)
		return false;
	const unsigned char* s = (const unsigned char*)utf8;
	while (*s) {
		uint32_t cp;
		s += utf8_decode(s, &cp);
		if (is_arabic_cp(cp))
			return true;
	}
	return false;
}

int TextShape_toVisual(const char* utf8_logical, char* out_visual, int out_sz) {
	if (!utf8_logical || !out_visual || out_sz <= 0)
		return 0;

	uint32_t in[TS_MAX], shaped[TS_MAX];
	int n = 0;
	const unsigned char* s = (const unsigned char*)utf8_logical;
	while (*s && n < TS_MAX) {
		uint32_t cp;
		s += utf8_decode(s, &cp);
		in[n++] = cp;
	}

	int m = reshape(in, n, shaped);

	uint32_t visual[TS_MAX];
	int v = bidi_reorder(shaped, m, visual);

	int len = 0;
	for (int i = 0; i < v; i++) {
		char buf[4];
		int b = utf8_encode(visual[i], buf);
		if (len + b >= out_sz)
			break;
		memcpy(out_visual + len, buf, b);
		len += b;
	}
	out_visual[len] = '\0';
	return len;
}
