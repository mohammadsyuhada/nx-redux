#include "tag_meta.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// Cap the decoded picture block: covers are well under a few MB and the value
// comes from an untrusted file on a small-RAM handheld.
#define TAG_PICTURE_MAX_BYTES (8 * 1024 * 1024)

static const struct {
	const char* key;
	TagField field;
} vorbis_keys[] = {
	{"TITLE", TAG_FIELD_TITLE},
	{"ARTIST", TAG_FIELD_ARTIST},
	{"ALBUM", TAG_FIELD_ALBUM},
	{"ALBUMARTIST", TAG_FIELD_ALBUM_ARTIST},
	{"ALBUM ARTIST", TAG_FIELD_ALBUM_ARTIST},
	{"METADATA_BLOCK_PICTURE", TAG_FIELD_PICTURE},
};

static int is_space(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

TagField TagMeta_vorbisField(const char* comment, size_t len, const char** value, size_t* value_len) {
	if (!comment || !value || !value_len)
		return TAG_FIELD_NONE;
	const char* eq = memchr(comment, '=', len);
	if (!eq)
		return TAG_FIELD_NONE;
	size_t key_len = (size_t)(eq - comment);

	TagField field = TAG_FIELD_NONE;
	for (size_t i = 0; i < sizeof(vorbis_keys) / sizeof(vorbis_keys[0]); i++) {
		if (strlen(vorbis_keys[i].key) == key_len && strncasecmp(comment, vorbis_keys[i].key, key_len) == 0) {
			field = vorbis_keys[i].field;
			break;
		}
	}
	if (field == TAG_FIELD_NONE)
		return TAG_FIELD_NONE;

	const char* start = eq + 1;
	const char* end = comment + len;
	while (start < end && is_space(*start))
		start++;
	while (end > start && (is_space(end[-1]) || end[-1] == '\0'))
		end--;
	if (start == end)
		return TAG_FIELD_NONE;

	*value = start;
	*value_len = (size_t)(end - start);
	return field;
}

static int base64_value(char c) {
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

// Decode base64, skipping whitespace and stopping at '=' padding.
// Returns the decoded byte count, or 0 on an invalid character.
static size_t base64_decode(const char* in, size_t len, uint8_t* out, size_t out_cap) {
	uint32_t acc = 0;
	int bits = 0;
	size_t n = 0;
	for (size_t i = 0; i < len && in[i] != '='; i++) {
		if (is_space(in[i]))
			continue;
		int v = base64_value(in[i]);
		if (v < 0)
			return 0;
		acc = (acc << 6) | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (n >= out_cap)
				return 0;
			out[n++] = (uint8_t)(acc >> bits);
		}
	}
	return n;
}

static uint32_t read_be32(const uint8_t* p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint8_t* TagMeta_decodeBlockPicture(const char* base64, size_t len, const uint8_t** image,
									size_t* image_size, uint32_t* type) {
	if (!base64 || !image || !image_size || !type)
		return NULL;

	size_t cap = len / 4 * 3 + 3;
	if (cap > TAG_PICTURE_MAX_BYTES)
		return NULL;
	uint8_t* block = malloc(cap);
	if (!block)
		return NULL;
	size_t size = base64_decode(base64, len, block, cap);

	// type(4) mime_len(4) mime desc_len(4) desc width/height/depth/colors(16) data_len(4) data
	if (size < 8)
		goto fail;
	uint32_t pic_type = read_be32(block);
	uint32_t mime_len = read_be32(block + 4);
	size_t pos = 8;
	if (mime_len > size - pos || size - pos - mime_len < 4)
		goto fail;
	pos += mime_len;
	uint32_t desc_len = read_be32(block + pos);
	pos += 4;
	if (desc_len > size - pos || size - pos - desc_len < 20)
		goto fail;
	pos += desc_len + 16;
	uint32_t data_len = read_be32(block + pos);
	pos += 4;
	if (data_len == 0 || data_len > size - pos)
		goto fail;

	*type = pic_type;
	*image = block + pos;
	*image_size = data_len;
	return block;

fail:
	free(block);
	return NULL;
}

// Append codepoint cp as UTF-8 at out[*j], keeping room for the terminator.
// Returns false (writing nothing) once it no longer fits.
static int put_utf8(char* out, size_t out_size, size_t* j, uint32_t cp) {
	char b[4];
	size_t n;
	if (cp < 0x80) {
		b[0] = (char)cp;
		n = 1;
	} else if (cp < 0x800) {
		b[0] = (char)(0xC0 | (cp >> 6));
		b[1] = (char)(0x80 | (cp & 0x3F));
		n = 2;
	} else if (cp < 0x10000) {
		b[0] = (char)(0xE0 | (cp >> 12));
		b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		b[2] = (char)(0x80 | (cp & 0x3F));
		n = 3;
	} else {
		b[0] = (char)(0xF0 | (cp >> 18));
		b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		b[3] = (char)(0x80 | (cp & 0x3F));
		n = 4;
	}
	if (*j + n >= out_size)
		return 0;
	memcpy(out + *j, b, n);
	*j += n;
	return 1;
}

void TagMeta_id3Text(uint8_t enc, const uint8_t* p, size_t len, char* out, size_t out_size) {
	if (!out || out_size == 0)
		return;
	out[0] = '\0';
	if (!p)
		return;

	size_t i = 0, j = 0;
	if (enc == 1 || enc == 2) {
		int be = enc == 2;
		if (enc == 1 && len >= 2) {
			if (p[0] == 0xFE && p[1] == 0xFF) {
				be = 1;
				i = 2;
			} else if (p[0] == 0xFF && p[1] == 0xFE) {
				i = 2;
			}
		}
		while (i + 1 < len) {
			uint32_t u = be ? ((uint32_t)p[i] << 8 | p[i + 1]) : ((uint32_t)p[i + 1] << 8 | p[i]);
			i += 2;
			if (u == 0)
				break;
			if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) {
				uint32_t lo = be ? ((uint32_t)p[i] << 8 | p[i + 1]) : ((uint32_t)p[i + 1] << 8 | p[i]);
				if (lo >= 0xDC00 && lo < 0xE000) {
					i += 2;
					u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
				}
			}
			if (u >= 0xD800 && u < 0xE000)
				u = 0xFFFD; // unpaired surrogate
			if (!put_utf8(out, out_size, &j, u))
				break;
		}
	} else if (enc == 0) {
		// Latin-1 maps 1:1 onto U+0000..U+00FF
		for (; i < len && p[i]; i++) {
			if (!put_utf8(out, out_size, &j, p[i]))
				break;
		}
	} else {
		// UTF-8: copy, but never cut a multi-byte sequence in half
		while (i < len && p[i]) {
			size_t n = 1;
			while (i + n < len && (p[i + n] & 0xC0) == 0x80)
				n++;
			if (j + n >= out_size)
				break;
			memcpy(out + j, p + i, n);
			i += n;
			j += n;
		}
	}
	out[j] = '\0';
}

// True when the first len bytes of p (up to a NUL) are well-formed UTF-8
static int is_utf8(const uint8_t* p, size_t len) {
	size_t i = 0;
	while (i < len && p[i]) {
		uint8_t c = p[i];
		size_t n;
		uint32_t cp;
		if (c < 0x80) {
			i++;
			continue;
		} else if ((c & 0xE0) == 0xC0) {
			n = 1;
			cp = c & 0x1F;
		} else if ((c & 0xF0) == 0xE0) {
			n = 2;
			cp = c & 0x0F;
		} else if ((c & 0xF8) == 0xF0) {
			n = 3;
			cp = c & 0x07;
		} else {
			return 0;
		}
		if (i + n >= len)
			return 0;
		for (size_t k = 1; k <= n; k++) {
			if ((p[i + k] & 0xC0) != 0x80)
				return 0;
			cp = (cp << 6) | (p[i + k] & 0x3F);
		}
		// Overlong forms, surrogates, beyond U+10FFFF
		if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) ||
			cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000))
			return 0;
		i += n + 1;
	}
	return 1;
}

void TagMeta_streamText(const char* src, size_t len, char* out, size_t out_size) {
	const uint8_t* p = (const uint8_t*)src;
	TagMeta_id3Text(is_utf8(p, len) ? 3 : 0, p, len, out, out_size);
}

int TagMeta_id3Picture(const uint8_t* frame, size_t size, const uint8_t** image, size_t* image_size,
					   uint32_t* type) {
	if (size < 2)
		return 0;
	uint8_t encoding = frame[0];
	size_t pos = 1;
	// MIME type is always Latin-1
	while (pos < size && frame[pos])
		pos++;
	pos++;
	if (pos >= size)
		return 0;
	uint8_t pic_type = frame[pos++];
	if (encoding == 1 || encoding == 2) {
		while (pos + 1 < size && (frame[pos] || frame[pos + 1]))
			pos += 2;
		pos += 2;
	} else {
		while (pos < size && frame[pos])
			pos++;
		pos++;
	}
	if (pos >= size)
		return 0;
	*image = frame + pos;
	*image_size = size - pos;
	*type = pic_type;
	return 1;
}
