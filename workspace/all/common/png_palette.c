#include "png_palette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

// ---- tuning ---------------------------------------------------------------

// Lloyd (k-means) passes after median cut: most of the gain is in the first
// two or three, each costs one nearest-colour search per histogram bucket.
#define KMEANS_PASSES 4
// Floyd-Steinberg at full strength turns flat areas that miss the palette by a
// shade into visible noise; 0.8 still breaks up gradient banding.
#define DITHER_STRENGTH 0.8f
// Cap on the error one pixel passes on per channel, so a pixel far from every
// palette colour (a lone highlight) doesn't smear across its neighbours.
#define DITHER_CLAMP 32.0f
// Nearest-colour cache for the dithering pass (exact ARGB keys, 64 K slots).
#define NEAREST_CACHE_BITS 16

#define ALPHA(p) ((uint8_t)((p) >> 24))
#define RED(p) ((uint8_t)((p) >> 16))
#define GREEN(p) ((uint8_t)((p) >> 8))
#define BLUE(p) ((uint8_t)(p))

// Every fully transparent pixel counts as this one colour: its RGB is never
// seen, so keeping the source's stray RGB values would only waste entries.
static inline uint32_t normalizePixel(uint32_t p) {
	return ALPHA(p) ? p : 0;
}

static inline uint32_t hashColor(uint32_t c) {
	c ^= c >> 16;
	c *= 0x7feb352dU;
	c ^= c >> 15;
	c *= 0x846ca68bU;
	c ^= c >> 16;
	return c;
}

// ---- exact palette (<= 256 colours) ---------------------------------------

// Small open-addressed map ARGB -> palette index; 1024 slots for at most 257
// keys keeps probes short.
#define EXACT_SLOTS 1024
typedef struct {
	uint32_t key[EXACT_SLOTS];
	int16_t idx[EXACT_SLOTS]; // -1 = empty
} ExactMap;

static int exactFind(ExactMap* m, uint32_t key, bool insert, int next_idx) {
	uint32_t slot = hashColor(key) & (EXACT_SLOTS - 1);
	for (;;) {
		if (m->idx[slot] < 0) {
			if (!insert)
				return -1;
			m->key[slot] = key;
			m->idx[slot] = (int16_t)next_idx;
			return next_idx;
		}
		if (m->key[slot] == key)
			return m->idx[slot];
		slot = (slot + 1) & (EXACT_SLOTS - 1);
	}
}

// Fill out->palette with the image's own colours and out->indices with exact
// lookups. False when there are more than 256 colours (nothing is changed
// that the caller relies on: it quantizes instead).
static bool tryExactPalette(const uint32_t* argb, int w, int h, int pitch_px, PngPaletteImage* out) {
	ExactMap* m = malloc(sizeof(*m));
	if (!m)
		return false;
	memset(m->idx, 0xff, sizeof(m->idx));
	int count = 0;
	for (int y = 0; y < h; y++) {
		const uint32_t* row = argb + (size_t)y * pitch_px;
		uint8_t* dst = out->indices + (size_t)y * w;
		uint32_t last = 0;
		int last_idx = -1;
		for (int x = 0; x < w; x++) {
			uint32_t c = normalizePixel(row[x]);
			// runs of one colour are the common case in pixel art and flat fills
			if (last_idx >= 0 && c == last) {
				dst[x] = (uint8_t)last_idx;
				continue;
			}
			int idx = exactFind(m, c, false, 0);
			if (idx < 0) {
				if (count == PNG_PALETTE_MAX) {
					free(m);
					return false;
				}
				idx = exactFind(m, c, true, count);
				out->palette[count] = (PngPaletteColor){RED(c), GREEN(c), BLUE(c), ALPHA(c)};
				count++;
			}
			dst[x] = (uint8_t)idx;
			last = c;
			last_idx = idx;
		}
	}
	free(m);
	out->count = count;
	out->exact = true;
	return true;
}

// ---- histogram ------------------------------------------------------------

// One histogram bucket: the pixels whose colour falls in a 5-bit-per-channel
// (and 4-bit alpha) cell, kept as weighted sums so the palette colours are
// true means rather than cell corners.
typedef struct {
	float c[4]; // r, g, b, a means
	float weight;
} HistEntry;

#define HIST_KEY_BITS 19 // a4 r5 g5 b5

static inline uint32_t histKey(uint32_t p) {
	return ((uint32_t)(ALPHA(p) >> 4) << 15) | ((uint32_t)(RED(p) >> 3) << 10) |
		   ((uint32_t)(GREEN(p) >> 3) << 5) | (uint32_t)(BLUE(p) >> 3);
}

// Build the histogram of the non-transparent pixels. *has_transparent reports
// whether any pixel had alpha 0 (those get their own palette entry).
static HistEntry* buildHistogram(const uint32_t* argb, int w, int h, int pitch_px, int* out_count,
								 bool* has_transparent) {
	int32_t* slot_of = calloc((size_t)1 << HIST_KEY_BITS, sizeof(int32_t)); // bucket + 1, 0 = none
	if (!slot_of)
		return NULL;
	int cap = 4096, count = 0;
	typedef struct {
		uint64_t sum[4];
		uint32_t n;
	} Acc;
	Acc* acc = malloc(sizeof(Acc) * cap);
	if (!acc) {
		free(slot_of);
		return NULL;
	}
	*has_transparent = false;
	for (int y = 0; y < h; y++) {
		const uint32_t* row = argb + (size_t)y * pitch_px;
		for (int x = 0; x < w; x++) {
			uint32_t p = row[x];
			if (!ALPHA(p)) {
				*has_transparent = true;
				continue;
			}
			uint32_t k = histKey(p);
			int b = slot_of[k] - 1;
			if (b < 0) {
				if (count == cap) {
					Acc* grown = realloc(acc, sizeof(Acc) * cap * 2);
					if (!grown) {
						free(acc);
						free(slot_of);
						return NULL;
					}
					acc = grown;
					cap *= 2;
				}
				b = count++;
				memset(&acc[b], 0, sizeof(Acc));
				slot_of[k] = b + 1;
			}
			acc[b].sum[0] += RED(p);
			acc[b].sum[1] += GREEN(p);
			acc[b].sum[2] += BLUE(p);
			acc[b].sum[3] += ALPHA(p);
			acc[b].n++;
		}
	}
	free(slot_of);

	HistEntry* hist = malloc(sizeof(HistEntry) * (count ? count : 1));
	if (!hist) {
		free(acc);
		return NULL;
	}
	for (int i = 0; i < count; i++) {
		float n = (float)acc[i].n;
		for (int c = 0; c < 4; c++)
			hist[i].c[c] = (float)acc[i].sum[c] / n;
		hist[i].weight = n;
	}
	free(acc);
	*out_count = count;
	return hist;
}

// ---- median cut -----------------------------------------------------------

typedef struct {
	int start, end; // range in the histogram array
	double sse;		// pixel-weighted squared error around the box mean (split priority)
	int axis;		// channel with the largest variance (split axis)
	float mean[4];
} Box;

static void boxStats(const HistEntry* hist, Box* box) {
	double w = 0, s[4] = {0}, ss[4] = {0};
	for (int i = box->start; i < box->end; i++) {
		double ew = hist[i].weight;
		w += ew;
		for (int c = 0; c < 4; c++) {
			s[c] += ew * hist[i].c[c];
			ss[c] += ew * hist[i].c[c] * hist[i].c[c];
		}
	}
	box->sse = 0;
	box->axis = 0;
	double best = -1;
	for (int c = 0; c < 4; c++) {
		double mean = w > 0 ? s[c] / w : 0;
		double var = w > 0 ? ss[c] - s[c] * mean : 0; // weighted SSE on this channel
		if (var < 0)
			var = 0;
		box->mean[c] = (float)mean;
		box->sse += var;
		if (var > best) {
			best = var;
			box->axis = c;
		}
	}
	// a single bucket can't be split, whatever its spread
	if (box->end - box->start < 2)
		box->sse = 0;
}

// One comparator per channel (qsort has no context argument, and a shared
// "current axis" global would race when two threads quantize at once).
#define COMPARE_ON(ch)                                       \
	static int compareOn##ch(const void* a, const void* b) { \
		float va = ((const HistEntry*)a)->c[ch];             \
		float vb = ((const HistEntry*)b)->c[ch];             \
		return (va > vb) - (va < vb);                        \
	}
COMPARE_ON(0)
COMPARE_ON(1)
COMPARE_ON(2)
COMPARE_ON(3)
static int (*const compareOnAxis[4])(const void*, const void*) = {compareOn0, compareOn1, compareOn2, compareOn3};

// Median cut down to at most `target` boxes; returns the number made.
static int medianCut(HistEntry* hist, int count, Box* boxes, int target) {
	int nboxes = 1;
	boxes[0] = (Box){.start = 0, .end = count};
	boxStats(hist, &boxes[0]);
	while (nboxes < target) {
		int pick = -1;
		double best = 0;
		for (int i = 0; i < nboxes; i++) {
			if (boxes[i].sse > best) {
				best = boxes[i].sse;
				pick = i;
			}
		}
		if (pick < 0)
			break; // every box is a single colour
		Box* box = &boxes[pick];
		qsort(hist + box->start, box->end - box->start, sizeof(HistEntry), compareOnAxis[box->axis]);
		// split at the weighted median, keeping both halves non-empty
		double total = 0;
		for (int i = box->start; i < box->end; i++)
			total += hist[i].weight;
		double half = total / 2, run = 0;
		int split = box->start + 1;
		for (int i = box->start; i < box->end - 1; i++) {
			run += hist[i].weight;
			split = i + 1;
			if (run >= half)
				break;
		}
		Box hi = {.start = split, .end = box->end};
		box->end = split;
		boxStats(hist, box);
		boxStats(hist, &hi);
		boxes[nboxes++] = hi;
	}
	return nboxes;
}

// ---- nearest colour ---------------------------------------------------------

static inline int nearestInt(const PngPaletteColor* pal, int count, int r, int g, int b, int a) {
	int best = 0;
	int best_d = 0x7fffffff;
	for (int i = 0; i < count; i++) {
		int dr = r - pal[i].r, dg = g - pal[i].g, db = b - pal[i].b, da = a - pal[i].a;
		int d = dr * dr + dg * dg + db * db + da * da;
		if (d < best_d) {
			best_d = d;
			best = i;
			if (d == 0)
				break;
		}
	}
	return best;
}

static inline int nearestFloat(const float (*pal)[4], int count, const float* c) {
	int best = 0;
	float best_d = 3.4e38f;
	for (int i = 0; i < count; i++) {
		float d0 = c[0] - pal[i][0], d1 = c[1] - pal[i][1], d2 = c[2] - pal[i][2], d3 = c[3] - pal[i][3];
		float d = d0 * d0 + d1 * d1 + d2 * d2 + d3 * d3;
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}
	return best;
}

// Lloyd refinement over the histogram: move each colour to the weighted mean
// of the buckets nearest to it. A colour that wins no bucket keeps its place.
static void kmeansRefine(const HistEntry* hist, int count, float (*pal)[4], int ncolors) {
	double (*sum)[5] = malloc(sizeof(double[5]) * ncolors);
	if (!sum)
		return;
	for (int pass = 0; pass < KMEANS_PASSES; pass++) {
		memset(sum, 0, sizeof(double[5]) * ncolors);
		for (int i = 0; i < count; i++) {
			int k = nearestFloat((const float (*)[4])pal, ncolors, hist[i].c);
			double w = hist[i].weight;
			for (int c = 0; c < 4; c++)
				sum[k][c] += w * hist[i].c[c];
			sum[k][4] += w;
		}
		for (int k = 0; k < ncolors; k++) {
			if (sum[k][4] <= 0)
				continue;
			for (int c = 0; c < 4; c++)
				pal[k][c] = (float)(sum[k][c] / sum[k][4]);
		}
	}
	free(sum);
}

static inline uint8_t clampByte(float v) {
	if (v <= 0)
		return 0;
	if (v >= 255)
		return 255;
	return (uint8_t)(v + 0.5f);
}

// ---- dithered mapping -------------------------------------------------------

typedef struct {
	uint32_t key; // ARGB with alpha > 0; 0 = empty slot
	uint8_t idx;
} CacheSlot;

static void mapDithered(const uint32_t* argb, int w, int h, int pitch_px, PngPaletteImage* out,
						int transparent_idx) {
	const PngPaletteColor* pal = out->palette;
	int count = out->count;
	CacheSlot* cache = calloc((size_t)1 << NEAREST_CACHE_BITS, sizeof(CacheSlot));
	// error rows (r, g, b) with one pixel of slack on each side
	float* err_cur = calloc((size_t)(w + 2) * 3, sizeof(float));
	float* err_next = calloc((size_t)(w + 2) * 3, sizeof(float));
	if (!cache || !err_cur || !err_next) {
		// out of memory: plain nearest mapping, no dithering
		for (int y = 0; y < h; y++) {
			const uint32_t* row = argb + (size_t)y * pitch_px;
			for (int x = 0; x < w; x++) {
				uint32_t p = row[x];
				out->indices[(size_t)y * w + x] =
					ALPHA(p) ? (uint8_t)nearestInt(pal, count, RED(p), GREEN(p), BLUE(p), ALPHA(p))
							 : (uint8_t)(transparent_idx < 0 ? 0 : transparent_idx);
			}
		}
		free(cache);
		free(err_cur);
		free(err_next);
		return;
	}
	const uint32_t cache_mask = ((uint32_t)1 << NEAREST_CACHE_BITS) - 1;

	for (int y = 0; y < h; y++) {
		const uint32_t* row = argb + (size_t)y * pitch_px;
		uint8_t* dst = out->indices + (size_t)y * w;
		// serpentine scan: alternate direction so the error doesn't drift one way
		bool rtl = y & 1;
		int dir = rtl ? -1 : 1;
		memset(err_next, 0, sizeof(float) * (w + 2) * 3);
		for (int i = 0; i < w; i++) {
			int x = rtl ? w - 1 - i : i;
			uint32_t p = row[x];
			if (!ALPHA(p)) {
				dst[x] = (uint8_t)transparent_idx;
				continue;
			}
			float* e = &err_cur[(x + 1) * 3];
			float want[3] = {RED(p) + e[0], GREEN(p) + e[1], BLUE(p) + e[2]};
			uint8_t r = clampByte(want[0]), g = clampByte(want[1]), b = clampByte(want[2]);
			uint8_t a = ALPHA(p);
			uint32_t key = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
			CacheSlot* slot = &cache[hashColor(key) & cache_mask];
			int idx;
			if (slot->key == key) {
				idx = slot->idx;
			} else {
				idx = nearestInt(pal, count, r, g, b, a);
				slot->key = key;
				slot->idx = (uint8_t)idx;
			}
			dst[x] = (uint8_t)idx;

			float qe[3] = {(want[0] - pal[idx].r) * DITHER_STRENGTH, (want[1] - pal[idx].g) * DITHER_STRENGTH,
						   (want[2] - pal[idx].b) * DITHER_STRENGTH};
			for (int c = 0; c < 3; c++) {
				float v = qe[c];
				if (v > DITHER_CLAMP)
					v = DITHER_CLAMP;
				else if (v < -DITHER_CLAMP)
					v = -DITHER_CLAMP;
				// 7/16 ahead, 3/16 behind-below, 5/16 below, 1/16 ahead-below
				err_cur[(x + 1 + dir) * 3 + c] += v * (7.0f / 16);
				err_next[(x + 1 - dir) * 3 + c] += v * (3.0f / 16);
				err_next[(x + 1) * 3 + c] += v * (5.0f / 16);
				err_next[(x + 1 + dir) * 3 + c] += v * (1.0f / 16);
			}
		}
		float* t = err_cur;
		err_cur = err_next;
		err_next = t;
	}
	free(cache);
	free(err_cur);
	free(err_next);
}

// ---- palette order ----------------------------------------------------------

// Translucent entries first (tRNS then only lists those), then the opaque ones.
static void sortTranslucentFirst(PngPaletteImage* img) {
	uint8_t remap[PNG_PALETTE_MAX];
	PngPaletteColor sorted[PNG_PALETTE_MAX];
	int n = 0;
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < img->count; i++) {
			bool translucent = img->palette[i].a < 255;
			if (translucent != (pass == 0))
				continue;
			remap[i] = (uint8_t)n;
			sorted[n++] = img->palette[i];
		}
	}
	bool identity = true;
	for (int i = 0; i < img->count; i++)
		if (remap[i] != i)
			identity = false;
	if (identity)
		return;
	memcpy(img->palette, sorted, sizeof(PngPaletteColor) * img->count);
	size_t total = (size_t)img->w * img->h;
	for (size_t i = 0; i < total; i++)
		img->indices[i] = remap[img->indices[i]];
}

// ---- quantize ---------------------------------------------------------------

bool PngPalette_quantize(const uint32_t* argb, int w, int h, int pitch_px, PngPaletteImage* out) {
	if (!argb || !out || w <= 0 || h <= 0 || pitch_px < w)
		return false;
	memset(out, 0, sizeof(*out));
	out->w = w;
	out->h = h;
	out->indices = malloc((size_t)w * h);
	if (!out->indices)
		return false;

	if (tryExactPalette(argb, w, h, pitch_px, out)) {
		sortTranslucentFirst(out);
		return true;
	}

	int hist_count = 0;
	bool has_transparent = false;
	HistEntry* hist = buildHistogram(argb, w, h, pitch_px, &hist_count, &has_transparent);
	if (!hist) {
		PngPalette_free(out);
		return false;
	}
	int target = PNG_PALETTE_MAX - (has_transparent ? 1 : 0);
	Box* boxes = malloc(sizeof(Box) * target);
	float (*pal)[4] = malloc(sizeof(float[4]) * target);
	if (!boxes || !pal) {
		free(boxes);
		free(pal);
		free(hist);
		PngPalette_free(out);
		return false;
	}
	int ncolors = 0;
	if (hist_count > 0) {
		ncolors = medianCut(hist, hist_count, boxes, target);
		for (int i = 0; i < ncolors; i++)
			memcpy(pal[i], boxes[i].mean, sizeof(pal[i]));
		kmeansRefine(hist, hist_count, pal, ncolors);
	}
	free(boxes);
	free(hist);

	for (int i = 0; i < ncolors; i++)
		out->palette[i] = (PngPaletteColor){clampByte(pal[i][0]), clampByte(pal[i][1]), clampByte(pal[i][2]),
											clampByte(pal[i][3])};
	free(pal);
	// a rounded translucent mean can land on 0 or 255 alpha; 0 would make an
	// entry look like the transparent one, so keep the visible ones at >= 1
	for (int i = 0; i < ncolors; i++)
		if (out->palette[i].a == 0)
			out->palette[i].a = 1;
	int transparent_idx = -1;
	if (has_transparent) {
		transparent_idx = ncolors;
		out->palette[ncolors++] = (PngPaletteColor){0, 0, 0, 0};
	}
	out->count = ncolors;
	out->exact = false;

	mapDithered(argb, w, h, pitch_px, out, transparent_idx);
	sortTranslucentFirst(out);
	return true;
}

void PngPalette_free(PngPaletteImage* img) {
	if (!img)
		return;
	free(img->indices);
	img->indices = NULL;
}

// ---- PNG encoding -------------------------------------------------------------

static void putBE32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

// Append one chunk (length, type, data, CRC over type + data) at *pos.
static void putChunk(uint8_t* buf, size_t* pos, const char* type, const uint8_t* data, uint32_t len) {
	uint8_t* p = buf + *pos;
	putBE32(p, len);
	memcpy(p + 4, type, 4);
	if (len)
		memcpy(p + 8, data, len);
	uint32_t crc = (uint32_t)crc32(0L, p + 4, len + 4);
	putBE32(p + 8 + len, crc);
	*pos += 12 + len;
}

bool PngPalette_encode(const PngPaletteImage* img, uint8_t** out_data, size_t* out_len) {
	if (!img || !img->indices || img->count < 1 || img->count > PNG_PALETTE_MAX || !out_data || !out_len)
		return false;
	int w = img->w, h = img->h;
	// small palettes pack several pixels per byte
	int depth = img->count <= 2 ? 1 : img->count <= 4 ? 2
								  : img->count <= 16  ? 4
													  : 8;
	size_t row_bytes = ((size_t)w * depth + 7) / 8;
	size_t raw_len = (row_bytes + 1) * h;
	uint8_t* raw = calloc(raw_len, 1);
	if (!raw)
		return false;
	// filter type 0 (none) on every row: the PNG spec's advice for palette
	// images, where the predictors rarely help and index deltas mean nothing
	for (int y = 0; y < h; y++) {
		uint8_t* dst = raw + y * (row_bytes + 1) + 1;
		const uint8_t* src = img->indices + (size_t)y * w;
		if (depth == 8) {
			memcpy(dst, src, w);
			continue;
		}
		int per_byte = 8 / depth;
		for (int x = 0; x < w; x++) {
			int shift = 8 - depth * (x % per_byte + 1);
			dst[x / per_byte] |= (uint8_t)(src[x] << shift);
		}
	}

	uLong bound = compressBound((uLong)raw_len);
	uint8_t* idat = malloc(bound);
	if (!idat) {
		free(raw);
		return false;
	}
	z_stream zs;
	memset(&zs, 0, sizeof(zs));
	if (deflateInit2(&zs, 9, Z_DEFLATED, 15, 9, Z_DEFAULT_STRATEGY) != Z_OK) {
		free(idat);
		free(raw);
		return false;
	}
	zs.next_in = raw;
	zs.avail_in = (uInt)raw_len;
	zs.next_out = idat;
	zs.avail_out = (uInt)bound;
	int zr = deflate(&zs, Z_FINISH);
	size_t idat_len = zs.total_out;
	deflateEnd(&zs);
	free(raw);
	if (zr != Z_STREAM_END) {
		free(idat);
		return false;
	}

	int trns_len = 0;
	for (int i = 0; i < img->count; i++)
		if (img->palette[i].a < 255)
			trns_len = i + 1;

	size_t total = 8 + (12 + 13) + (12 + 3 * (size_t)img->count) + (trns_len ? 12 + (size_t)trns_len : 0) +
				   (12 + idat_len) + 12;
	uint8_t* buf = malloc(total);
	if (!buf) {
		free(idat);
		return false;
	}
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	memcpy(buf, sig, 8);
	size_t pos = 8;

	uint8_t ihdr[13];
	putBE32(ihdr, (uint32_t)w);
	putBE32(ihdr + 4, (uint32_t)h);
	ihdr[8] = (uint8_t)depth;
	ihdr[9] = 3;  // colour type: palette
	ihdr[10] = 0; // deflate
	ihdr[11] = 0; // adaptive filtering (every row uses filter 0)
	ihdr[12] = 0; // not interlaced
	putChunk(buf, &pos, "IHDR", ihdr, 13);

	uint8_t plte[3 * PNG_PALETTE_MAX], trns[PNG_PALETTE_MAX];
	for (int i = 0; i < img->count; i++) {
		plte[i * 3] = img->palette[i].r;
		plte[i * 3 + 1] = img->palette[i].g;
		plte[i * 3 + 2] = img->palette[i].b;
		trns[i] = img->palette[i].a;
	}
	putChunk(buf, &pos, "PLTE", plte, (uint32_t)(3 * img->count));
	if (trns_len)
		putChunk(buf, &pos, "tRNS", trns, (uint32_t)trns_len);
	putChunk(buf, &pos, "IDAT", idat, (uint32_t)idat_len);
	putChunk(buf, &pos, "IEND", NULL, 0);
	free(idat);

	*out_data = buf;
	*out_len = pos;
	return true;
}

// ---- files --------------------------------------------------------------------

bool PngPalette_writeFileAtomic(const char* path, const uint8_t* data, size_t len) {
	if (!path || !data)
		return false;
	char tmp[1024];
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return false;
	FILE* f = fopen(tmp, "wb");
	if (!f)
		return false;
	bool ok = fwrite(data, 1, len, f) == len;
	ok = fflush(f) == 0 && ok;
	// on the card the rename must not land before the data does
	ok = fsync(fileno(f)) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	if (!ok) {
		remove(tmp);
		return false;
	}
	if (rename(tmp, path) != 0) {
		// some FAT drivers refuse to rename over an existing file: move the original aside first, and put it back if
		// the replacement still can't take its place (the original is never lost, only the new copy)
		char bak[1024];
		if (snprintf(bak, sizeof(bak), "%s.bak", path) >= (int)sizeof(bak) || rename(path, bak) != 0) {
			remove(tmp);
			return false;
		}
		if (rename(tmp, path) != 0) {
			rename(bak, path);
			remove(tmp);
			return false;
		}
		remove(bak);
	}
	return true;
}

bool PngPalette_saveARGB(const uint32_t* argb, int w, int h, int pitch_px, const char* path) {
	PngPaletteImage img;
	if (!PngPalette_quantize(argb, w, h, pitch_px, &img))
		return false;
	uint8_t* data = NULL;
	size_t len = 0;
	bool ok = PngPalette_encode(&img, &data, &len);
	PngPalette_free(&img);
	if (ok)
		ok = PngPalette_writeFileAtomic(path, data, len);
	free(data);
	return ok;
}

bool PngPalette_isIndexedFile(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return false;
	uint8_t head[33];
	size_t n = fread(head, 1, sizeof(head), f);
	fclose(f);
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	if (n < sizeof(head) || memcmp(head, sig, 8) != 0 || memcmp(head + 12, "IHDR", 4) != 0)
		return false;
	return head[25] == 3; // IHDR colour type
}
