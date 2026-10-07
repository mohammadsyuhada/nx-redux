// scraper_optimize.h - the Artwork Manager's "Optimize images" pass.
//
// Rewrites the game art already on the card as 256-colour PNGs (png_palette),
// the format the scraper now saves new downloads in: same width and height,
// fewer colours, a third of the bytes or less. Covers every .media folder
// under Roms (nested game folders too): the root pictures (older scrapes,
// PortMaster and hand-made art) and the screenshot/ and boxart/ variants.
// Folder backgrounds (bg.png and friends) are left alone.
#ifndef SCRAPER_OPTIMIZE_H
#define SCRAPER_OPTIMIZE_H

#include <stdbool.h>

typedef struct {
	char** paths; // absolute PNG paths, sorted (so progress goes system by system)
	int count;
} OptimizeList;

// Collect every art PNG under roms_root. False only when out of memory; an
// empty list (no art at all) is a success.
bool Optimize_collect(const char* roms_root, OptimizeList* out);
void Optimize_freeList(OptimizeList* list);

typedef enum {
	OPTIMIZE_DONE,	  // replaced with a smaller 256-colour PNG
	OPTIMIZE_ALREADY, // already a palette PNG: not touched
	OPTIMIZE_NO_GAIN, // the 256-colour version was not smaller: original kept
	OPTIMIZE_FAILED,  // could not be read or written: original kept
} OptimizeResult;

// Convert one PNG in place (tmp file + rename, only when smaller). On
// OPTIMIZE_DONE *saved_bytes gets the bytes saved. One image in memory at a
// time; safe to call from a worker thread.
OptimizeResult Optimize_file(const char* path, long long* saved_bytes);

#endif // SCRAPER_OPTIMIZE_H
