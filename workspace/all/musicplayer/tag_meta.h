#ifndef __TAG_META_H__
#define __TAG_META_H__

#include <stddef.h>
#include <stdint.h>

// Picture type of a front cover in ID3 APIC / FLAC PICTURE blocks
#define TAG_PICTURE_FRONT_COVER 3

// Vorbis comment fields the player uses (FLAC, Ogg Vorbis, Opus)
typedef enum {
	TAG_FIELD_NONE = 0,
	TAG_FIELD_TITLE,
	TAG_FIELD_ARTIST,
	TAG_FIELD_ALBUM,
	TAG_FIELD_ALBUM_ARTIST,
	TAG_FIELD_PICTURE, // METADATA_BLOCK_PICTURE (base64 FLAC PICTURE block)
} TagField;

// Classify one "KEY=value" Vorbis comment of len bytes (need not be
// NUL-terminated). Keys match case-insensitively. On a known field, *value and
// *value_len give the value with surrounding whitespace trimmed; an empty value
// is reported as TAG_FIELD_NONE so it never replaces a filename-derived title.
TagField TagMeta_vorbisField(const char* comment, size_t len, const char** value, size_t* value_len);

// Decode a METADATA_BLOCK_PICTURE value of len bytes (a base64-encoded FLAC
// PICTURE block, used by Ogg Vorbis and Opus for cover art).
// Returns a malloc'd buffer owning the decoded block (caller frees) with
// *image/*image_size pointing at the image bytes inside it and *type set to
// the picture type, or NULL if the value is malformed.
uint8_t* TagMeta_decodeBlockPicture(const char* base64, size_t len, const uint8_t** image,
									size_t* image_size, uint32_t* type);

// Locate the image inside the size bytes of an ID3v2.3/2.4 APIC frame body
// (encoding, MIME type, picture type, description, image data). UTF-16
// descriptions end on an aligned 2-byte terminator. Returns 0 if the frame is
// truncated or holds no image bytes.
int TagMeta_id3Picture(const uint8_t* frame, size_t size, const uint8_t** image, size_t* image_size,
					   uint32_t* type);

// Decode the text of an ID3v2 text frame (encoding enc: 0 Latin-1, 1 UTF-16
// with BOM, 2 UTF-16BE, 3 UTF-8) from len bytes at p into out as UTF-8,
// stopping at the first terminator. Output is NUL-terminated and truncated on
// a character boundary to fit out_size.
void TagMeta_id3Text(uint8_t enc, const uint8_t* p, size_t len, char* out, size_t out_size);

// Copy untagged stream text (ICY StreamTitle, icy-name; no declared charset)
// from up to len bytes at src into out as UTF-8: kept as-is when it is valid
// UTF-8, otherwise read as Latin-1. Same termination/truncation as above.
void TagMeta_streamText(const char* src, size_t len, char* out, size_t out_size);

#endif
