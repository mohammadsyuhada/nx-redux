#ifndef IV_PROBE_H
#define IV_PROBE_H
#include <stdbool.h>
// Read image dimensions from the file header without decoding pixels.
// Supports PNG, JPEG (SOF0-SOF15 minus C4/C8/CC), GIF, BMP.
// Returns false (and leaves *w/*h untouched) for unknown/corrupt headers.
bool IvProbe_dimensions(const char* path, int* w, int* h);
#endif
