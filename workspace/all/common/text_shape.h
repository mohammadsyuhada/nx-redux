#ifndef __TEXT_SHAPE_H__
#define __TEXT_SHAPE_H__

#include <stdbool.h>

// Userspace Arabic text shaping for the no-HarfBuzz SDL_ttf build. Transforms a
// logical UTF-8 string into a display-ready UTF-8 string: Arabic base letters
// are replaced by their contextual presentation forms (U+FE70..FEFF, incl.
// lam-alef ligatures) and — once BiDi lands — reordered for visual display.
// Non-Arabic codepoints pass through unchanged. See
// docs/superpowers/specs/2026-08-13-arabic-text-rendering-design.md.

// True if s contains any Arabic-script codepoint (cheap pre-check; callers use
// it to skip all shaping for the non-Arabic common case).
bool TextShape_hasArabic(const char* utf8);

// Base paragraph direction: true if the first strong character is RTL (Arabic).
// LTR (false) otherwise, including all-neutral/empty. Used to pick marquee
// scroll direction and alignment for a string.
bool TextShape_baseIsRTL(const char* utf8);

// Write the shaped (and, from Task 2 on, BiDi-reordered) form of utf8_logical
// into out_visual (<= out_sz-1 bytes + NUL). Returns the output byte length, or
// 0 on empty/overflow. Safe to call on any UTF-8; non-Arabic is passed through.
int TextShape_toVisual(const char* utf8_logical, char* out_visual, int out_sz);

#endif // __TEXT_SHAPE_H__
