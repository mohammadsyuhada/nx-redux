#ifndef PNG_PALETTE_H
#define PNG_PALETTE_H

// Game art as 256-colour (indexed) PNGs: the same width and height as the
// source, roughly a third to a fifth of the bytes of a 32-bit PNG. The
// Artwork Manager saves every screenshot and box art it downloads this way
// and its "Optimize images" pass converts the art already on the card.
//
// Quantizer: median cut over a 5-bit-per-channel histogram (the box with the
// largest pixel-weighted variance splits first, at its weighted median),
// refined by a few k-means passes, then mapped with Floyd-Steinberg dithering
// at reduced strength (gradients don't band, flat areas stay clean). An image
// that already has 256 colours or fewer keeps them exactly, undithered.
// Alpha is kept: fully transparent pixels share one palette entry and
// translucent colours go to the PNG's tRNS chunk.
//
// Depends on libc and zlib only (no SDL), so the host unit tests can link it.
// Pixels are ARGB8888 (0xAARRGGBB in a uint32_t, SDL_PIXELFORMAT_ARGB8888).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PNG_PALETTE_MAX 256

typedef struct {
	uint8_t r, g, b, a;
} PngPaletteColor;

typedef struct {
	int w, h;
	uint8_t* indices; // w * h palette indices, row-major (no padding)
	PngPaletteColor palette[PNG_PALETTE_MAX];
	int count;	// palette entries used (1..256)
	bool exact; // true when the source had <= 256 colours (kept as-is)
} PngPaletteImage;

// Quantize w x h ARGB8888 pixels (pitch_px pixels per source row) into `out`.
// Translucent palette entries are sorted first, so tRNS stays short. False on
// bad arguments or out of memory; on success free `out` with PngPalette_free.
bool PngPalette_quantize(const uint32_t* argb, int w, int h, int pitch_px, PngPaletteImage* out);
void PngPalette_free(PngPaletteImage* img);

// Encode an indexed image as a complete PNG file in memory (IHDR, PLTE, tRNS
// when needed, one IDAT at deflate level 9, IEND). Palettes of 16 colours or
// fewer use 4/2/1-bit pixels. On success *out_data is malloc'd (caller frees).
bool PngPalette_encode(const PngPaletteImage* img, uint8_t** out_data, size_t* out_len);

// Write `len` bytes to <path>.tmp, fsync, then rename it over `path`, so the
// target is never left half-written (FAT/exFAT: when the rename refuses to
// replace an existing file, the old one is moved to <path>.bak first and put
// back if the replacement still fails, so it is never lost). The tmp file is
// removed on failure.
bool PngPalette_writeFileAtomic(const char* path, const uint8_t* data, size_t len);

// Quantize + encode + atomic write in one call (the scraper's save path).
bool PngPalette_saveARGB(const uint32_t* argb, int w, int h, int pitch_px, const char* path);

// True when `path` is a PNG whose IHDR says colour type 3 (palette), from the
// first 33 bytes only: lets the optimize pass skip converted files quickly.
bool PngPalette_isIndexedFile(const char* path);

#endif // PNG_PALETTE_H
