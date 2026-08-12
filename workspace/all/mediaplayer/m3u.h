#ifndef __M3U_H__
#define __M3U_H__

#include "iptv_curated.h" // CuratedTVChannel, IPTV_MAX_*

// Parse an M3U/M3U8 playlist file into channels.
// Each #EXTINF line supplies name (text after the last comma), tvg-logo -> logo,
// group-title -> category; the next non-comment, non-blank line is the URL.
// country_code is stamped onto every parsed channel (may be NULL).
// Returns the number of channels written (0..max), or -1 if the file cannot be opened.
int M3U_parseFile(const char* path, CuratedTVChannel* out, int max, const char* country_code);

#endif // __M3U_H__
