// Host test for common/png_palette.c: the Artwork Manager's 256-colour PNG
// writer. Images with <= 256 colours must round-trip exactly; a smooth
// gradient must come back within a small mean error; alpha (transparent and
// translucent) must survive; the files must be valid indexed PNGs, which a
// small zlib-based decoder here reads back and checks chunk by chunk (CRCs,
// colour type, bit depth, PLTE/tRNS sizes).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "../png_palette.h"

#define ARGB(a, r, g, b) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

static uint32_t be32(const uint8_t* p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

typedef struct {
	int w, h, depth, color_type;
	int plte_count, trns_count;
	uint32_t* argb; // decoded pixels
} Decoded;

// Decode the subset of PNG the writer produces (colour type 3, filter 0 rows,
// not interlaced), validating every chunk CRC on the way.
static Decoded decodePNG(const uint8_t* data, size_t len) {
	Decoded d = {0};
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	assert(len > 8 && memcmp(data, sig, 8) == 0);
	uint8_t pal[256][4];
	for (int i = 0; i < 256; i++)
		pal[i][3] = 255;
	uint8_t* idat = NULL;
	size_t idat_len = 0;
	size_t pos = 8;
	bool seen_iend = false;
	while (pos + 12 <= len) {
		uint32_t clen = be32(data + pos);
		const uint8_t* type = data + pos + 4;
		const uint8_t* body = data + pos + 8;
		assert(pos + 12 + clen <= len);
		uint32_t crc = (uint32_t)crc32(0L, type, clen + 4);
		assert(crc == be32(body + clen));
		if (!memcmp(type, "IHDR", 4)) {
			assert(clen == 13);
			d.w = (int)be32(body);
			d.h = (int)be32(body + 4);
			d.depth = body[8];
			d.color_type = body[9];
			assert(body[10] == 0 && body[11] == 0 && body[12] == 0);
		} else if (!memcmp(type, "PLTE", 4)) {
			assert(clen % 3 == 0 && clen / 3 >= 1 && clen / 3 <= 256);
			d.plte_count = (int)(clen / 3);
			for (int i = 0; i < d.plte_count; i++) {
				pal[i][0] = body[i * 3];
				pal[i][1] = body[i * 3 + 1];
				pal[i][2] = body[i * 3 + 2];
			}
		} else if (!memcmp(type, "tRNS", 4)) {
			assert(d.plte_count > 0 && (int)clen <= d.plte_count);
			d.trns_count = (int)clen;
			for (uint32_t i = 0; i < clen; i++)
				pal[i][3] = body[i];
		} else if (!memcmp(type, "IDAT", 4)) {
			idat = realloc(idat, idat_len + clen);
			memcpy(idat + idat_len, body, clen);
			idat_len += clen;
		} else if (!memcmp(type, "IEND", 4)) {
			seen_iend = true;
		}
		pos += 12 + clen;
	}
	assert(seen_iend && pos == len);
	assert(d.color_type == 3);
	assert(d.depth == 1 || d.depth == 2 || d.depth == 4 || d.depth == 8);
	assert(d.plte_count > 0 && d.plte_count <= (1 << d.depth));

	size_t row_bytes = ((size_t)d.w * d.depth + 7) / 8;
	uLongf raw_len = (uLongf)((row_bytes + 1) * d.h);
	uint8_t* raw = malloc(raw_len);
	assert(uncompress(raw, &raw_len, idat, (uLong)idat_len) == Z_OK);
	assert(raw_len == (row_bytes + 1) * d.h);
	free(idat);

	d.argb = malloc(sizeof(uint32_t) * d.w * d.h);
	for (int y = 0; y < d.h; y++) {
		const uint8_t* row = raw + y * (row_bytes + 1);
		assert(row[0] == 0); // filter none
		for (int x = 0; x < d.w; x++) {
			int per_byte = 8 / d.depth;
			int shift = 8 - d.depth * (x % per_byte + 1);
			int idx = (row[1 + x / per_byte] >> shift) & ((1 << d.depth) - 1);
			assert(idx < d.plte_count);
			d.argb[y * d.w + x] = ARGB(pal[idx][3], pal[idx][0], pal[idx][1], pal[idx][2]);
		}
	}
	free(raw);
	return d;
}

static Decoded encodeAndDecode(const uint32_t* px, int w, int h, int pitch, PngPaletteImage* img_out) {
	PngPaletteImage img;
	assert(PngPalette_quantize(px, w, h, pitch, &img));
	assert(img.count >= 1 && img.count <= 256);
	uint8_t* data = NULL;
	size_t len = 0;
	assert(PngPalette_encode(&img, &data, &len));
	Decoded d = decodePNG(data, len);
	free(data);
	assert(d.w == w && d.h == h);
	if (img_out)
		*img_out = img;
	else
		PngPalette_free(&img);
	return d;
}

static uint32_t norm(uint32_t p) {
	return (p >> 24) ? p : 0;
}

// <= 256 colours, opaque, translucent and transparent, a padded pitch and a
// width that isn't a multiple of anything: decoded pixels equal the source.
static void testExactRoundTrip(void) {
	int w = 37, h = 23, pitch = 40;
	uint32_t* px = calloc(pitch * h, sizeof(uint32_t));
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int k = (x * 7 + y * 3) % 200;
			uint32_t c = ARGB(255, k, 255 - k, (k * 5) & 255);
			if (k % 50 == 1)
				c = ARGB(128, k, 0, 0); // translucent
			if (k % 50 == 2)
				c = ARGB(0, k, k, k); // transparent: RGB is irrelevant
			px[y * pitch + x] = c;
		}
	PngPaletteImage img;
	Decoded d = encodeAndDecode(px, w, h, pitch, &img);
	assert(img.exact);
	assert(d.depth == 8);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			assert(d.argb[y * w + x] == norm(px[y * pitch + x]));
	// translucent entries come first and tRNS covers exactly those
	assert(d.trns_count > 0);
	for (int i = 0; i < img.count; i++)
		assert((img.palette[i].a < 255) == (i < d.trns_count));
	PngPalette_free(&img);
	free(d.argb);
	free(px);
}

// Few colours pack into 1/2/4-bit pixels (odd widths exercise partial bytes).
static void testSmallPalettes(void) {
	int counts[] = {1, 2, 3, 4, 5, 16, 17};
	int depths[] = {1, 1, 2, 2, 4, 4, 8};
	for (size_t t = 0; t < sizeof(counts) / sizeof(counts[0]); t++) {
		int w = 13, h = 5, n = counts[t];
		uint32_t px[13 * 5];
		for (int i = 0; i < w * h; i++)
			px[i] = ARGB(255, (i % n) * 13, 200 - (i % n) * 7, 9);
		Decoded d = encodeAndDecode(px, w, h, w, NULL);
		assert(d.depth == depths[t]);
		assert(d.trns_count == 0);
		for (int i = 0; i < w * h; i++)
			assert(d.argb[i] == px[i]);
		free(d.argb);
	}
}

// A smooth 2D gradient with far more than 256 colours: quantized, at most 256
// entries, small mean error and no large error on any pixel.
static void testGradient(void) {
	int w = 320, h = 240;
	uint32_t* px = malloc(sizeof(uint32_t) * w * h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			px[y * w + x] = ARGB(255, x * 255 / (w - 1), y * 255 / (h - 1), (x + y) * 255 / (w + h - 2));
	PngPaletteImage img;
	Decoded d = encodeAndDecode(px, w, h, w, &img);
	assert(!img.exact);
	assert(d.plte_count <= 256 && d.trns_count == 0);
	double total = 0;
	int worst = 0;
	for (int i = 0; i < w * h; i++) {
		uint32_t a = px[i], b = d.argb[i];
		assert((b >> 24) == 255);
		for (int s = 0; s < 24; s += 8) {
			int diff = abs((int)((a >> s) & 255) - (int)((b >> s) & 255));
			total += diff;
			if (diff > worst)
				worst = diff;
		}
	}
	double mean = total / (w * h * 3.0);
	// dithering trades per-pixel error for the right average colour: compare
	// 4x4 block means too, which is what the eye sees on the device screen
	double block_total = 0;
	int blocks = 0;
	for (int by = 0; by + 4 <= h; by += 4)
		for (int bx = 0; bx + 4 <= w; bx += 4) {
			for (int s = 0; s < 24; s += 8) {
				int sa = 0, sb = 0;
				for (int y = by; y < by + 4; y++)
					for (int x = bx; x < bx + 4; x++) {
						sa += (px[y * w + x] >> s) & 255;
						sb += (d.argb[y * w + x] >> s) & 255;
					}
				block_total += abs(sa - sb) / 16.0;
			}
			blocks++;
		}
	double block_mean = block_total / (blocks * 3.0);
	printf("  gradient: %d colours, mean abs error %.2f (4x4 blocks %.2f), worst %d\n", img.count, mean,
		   block_mean, worst);
	assert(mean < 6.0);
	assert(block_mean < 2.0);
	assert(worst < 40);
	PngPalette_free(&img);
	free(d.argb);
	free(px);
}

// Box-art style: a noisy opaque picture with transparent corners and a
// translucent edge. Transparent stays fully transparent, opaque stays opaque,
// translucent keeps its alpha to within about one 4-bit histogram cell.
static void testAlpha(void) {
	int w = 200, h = 150;
	uint32_t* px = malloc(sizeof(uint32_t) * w * h);
	unsigned seed = 1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			seed = seed * 1103515245u + 12345u;
			int n = (seed >> 16) & 31;
			uint32_t c = ARGB(255, (x + n) & 255, (y * 2 + n) & 255, (x ^ y) & 255);
			if (x < 10 && y < 10)
				c = ARGB(0, 1, 2, 3);
			else if (x == w - 1)
				c = ARGB(96 + (y % 64), 200, 100, 50);
			px[y * w + x] = c;
		}
	PngPaletteImage img;
	Decoded d = encodeAndDecode(px, w, h, w, &img);
	assert(!img.exact && d.trns_count > 0);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int sa = px[y * w + x] >> 24, da = d.argb[y * w + x] >> 24;
			if (sa == 0)
				assert(da == 0);
			else if (sa == 255)
				assert(da == 255);
			else
				assert(abs(sa - da) <= 20 && da > 0);
		}
	PngPalette_free(&img);
	free(d.argb);
	free(px);
}

static uint8_t* readFile(const char* path, size_t* len) {
	FILE* f = fopen(path, "rb");
	assert(f);
	fseek(f, 0, SEEK_END);
	*len = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* buf = malloc(*len);
	assert(fread(buf, 1, *len, f) == *len);
	fclose(f);
	return buf;
}

// saveARGB writes atomically over an existing file, leaves no .tmp behind and
// the result is recognised as indexed; a truecolour PNG header is not.
static void testFiles(void) {
	char path[256], tmp[300];
	snprintf(path, sizeof(path), "/tmp/nx_test_png_palette_%d.png", (int)getpid());
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	// a truecolour (colour type 6) PNG header is not indexed
	static const uint8_t rgba_head[33] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
										  0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0, 0, 0, 0, 0};
	FILE* f = fopen(path, "wb");
	fwrite(rgba_head, 1, sizeof(rgba_head), f);
	fclose(f);
	assert(!PngPalette_isIndexedFile(path));

	int w = 64, h = 48;
	uint32_t* px = malloc(sizeof(uint32_t) * w * h);
	for (int i = 0; i < w * h; i++)
		px[i] = ARGB(255, i & 255, (i >> 3) & 255, (i * 7) & 255);
	assert(PngPalette_saveARGB(px, w, h, w, path));
	assert(access(tmp, F_OK) != 0);
	assert(PngPalette_isIndexedFile(path));

	size_t len;
	uint8_t* data = readFile(path, &len);
	Decoded d = decodePNG(data, len);
	assert(d.w == w && d.h == h && d.color_type == 3);
	free(d.argb);
	free(data);

	remove(path);
	assert(!PngPalette_isIndexedFile(path)); // missing file

	// bad arguments
	PngPaletteImage img;
	assert(!PngPalette_quantize(NULL, 1, 1, 1, &img));
	assert(!PngPalette_quantize(px, 0, 1, 1, &img));
	assert(!PngPalette_quantize(px, 4, 1, 3, &img)); // pitch shorter than a row
	free(px);
}

int main(void) {
	testExactRoundTrip();
	testSmallPalettes();
	testGradient();
	testAlpha();
	testFiles();
	printf("test_png_palette: all passed\n");
	return 0;
}
