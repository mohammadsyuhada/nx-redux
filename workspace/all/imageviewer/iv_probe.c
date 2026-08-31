#include <stdio.h>
#include <string.h>

#include "iv_probe.h"

// Enough to reach the SOF marker even behind a fat EXIF/ICC APP segment.
#define PROBE_BUF_SIZE (32 * 1024)

static unsigned int read_be16(const unsigned char* p) {
	return ((unsigned int)p[0] << 8) | p[1];
}

static unsigned int read_le16(const unsigned char* p) {
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int read_be32(const unsigned char* p) {
	return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3];
}

static int read_le32(const unsigned char* p) {
	unsigned int v = (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
	return (int)v;
}

static bool probe_png(const unsigned char* buf, size_t n, int* w, int* h) {
	if (n < 24)
		return false;
	int width = (int)read_be32(buf + 16);
	int height = (int)read_be32(buf + 20);
	if (width <= 0 || height <= 0)
		return false;
	*w = width;
	*h = height;
	return true;
}

static bool probe_gif(const unsigned char* buf, size_t n, int* w, int* h) {
	if (n < 10)
		return false;
	int width = (int)read_le16(buf + 6);
	int height = (int)read_le16(buf + 8);
	if (width <= 0 || height <= 0)
		return false;
	*w = width;
	*h = height;
	return true;
}

static bool probe_bmp(const unsigned char* buf, size_t n, int* w, int* h) {
	if (n < 26)
		return false;
	int width = read_le32(buf + 18);
	int height = read_le32(buf + 22);
	if (height < 0)
		height = -height; // top-down BMPs store a negative height
	if (width <= 0 || height <= 0)
		return false;
	*w = width;
	*h = height;
	return true;
}

// Marker walk starting past SOI. Stops at the first SOFx (frame header
// holding the real dimensions) or at SOS/buffer end.
static bool probe_jpeg(const unsigned char* buf, size_t n, int* w, int* h) {
	size_t i = 2;
	while (i < n) {
		if (buf[i] != 0xFF)
			return false; // not a marker where one is expected

		while (i < n && buf[i] == 0xFF) // skip 0xFF fill bytes
			i++;
		if (i >= n)
			return false;
		unsigned char m = buf[i];
		i++;

		if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
			// SOFx: len(2) precision(1) height(2) width(2), big-endian
			if (i + 7 > n)
				return false;
			int height = (int)read_be16(buf + i + 3);
			int width = (int)read_be16(buf + i + 5);
			if (width <= 0 || height <= 0)
				return false;
			*w = width;
			*h = height;
			return true;
		}

		if (m == 0xD8 || m == 0xD9 || m == 0x01 || (m >= 0xD0 && m <= 0xD7))
			continue; // standalone markers, no length field

		if (m == 0xDA)
			return false; // SOS: entropy-coded data follows, no more markers

		if (i + 2 > n)
			return false;
		unsigned int seg_len = read_be16(buf + i);
		if (seg_len < 2 || i + seg_len > n)
			return false;
		i += seg_len;
	}
	return false;
}

bool IvProbe_dimensions(const char* path, int* w, int* h) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return false;

	unsigned char buf[PROBE_BUF_SIZE];
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);

	if (n >= 8 && memcmp(buf, "\x89PNG\r\n\x1a\n", 8) == 0)
		return probe_png(buf, n, w, h);
	if (n >= 6 && (memcmp(buf, "GIF87a", 6) == 0 || memcmp(buf, "GIF89a", 6) == 0))
		return probe_gif(buf, n, w, h);
	if (n >= 2 && buf[0] == 'B' && buf[1] == 'M')
		return probe_bmp(buf, n, w, h);
	if (n >= 2 && buf[0] == 0xFF && buf[1] == 0xD8)
		return probe_jpeg(buf, n, w, h);

	return false;
}
