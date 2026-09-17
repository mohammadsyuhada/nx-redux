#pragma once
#include <stddef.h>

// Pure cheat-file matcher. No dependency on minarch's core/game state, so it
// is unit-testable with a plain host compiler.
//
// Given a ROM's region-stripped display name (rom_display, e.g. from
// getDisplayName) and its full name carrying region tags (rom_fullname, e.g.
// game.alt_name -> "Advance Wars (USA, Europe)"), rank the candidate .cht
// basenames and select the best-matching game+region group. The selected
// indices are the variant files to merge (same game+region, differing only by
// cheat source such as "(Code Breaker)" vs "(GameShark)"), best-first.
//
// Returns the number of selected indices written to out_indices (capacity
// out_cap). Returns 0 when no candidate's stem exactly matches rom_display, in
// which case the caller keeps its legacy first-existing-hit behaviour.
int CheatMatch_select(const char* rom_display, const char* rom_fullname,
					  const char* const* candidate_basenames, int n,
					  int* out_indices, int out_cap);

// Extract the cheat-source label from a basename, preserving original case,
// e.g. "Contra (USA) (GameShark).cht" -> "GameShark". Writes "" when the file
// carries no recognised source tag (only region/version tags, or none).
void CheatMatch_sourceLabel(const char* basename, char* out, size_t out_len);
