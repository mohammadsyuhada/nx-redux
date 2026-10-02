#ifndef LIST_WINDOW_H
#define LIST_WINDOW_H

#include <stdbool.h>

// The List's visible window [start, end) over `total` entries with `rows` rows on screen, and the selection that
// moves inside it. SDL-free so it can be host-tested (common/tests/test_list_window.c). Every body is a verbatim
// transcription of the inline block it replaces; the caller is named on each function in list_window.c.
// `rows` is clamped to >= 1 inside (the callers always pass >= 1, so this changes nothing for them).
//
// A "valid" window is 0 <= start, end = start + min(total, rows) <= total -- what every builder below produces.
//
// Reconciliations (where callers' blocks differed):
// - selectAtTop is the view clamp (gridview selectTile, rowview LEFT/RIGHT, stackview selectItem), which clamps
//   start at 0. The List letter jump (gamelist L1/R1) wraps the same steps in `if (total > rows)` and never clamps:
//   for total > rows the two are identical on every input (end - rows >= 0 there), so wave 2 keeps that guard
//   around the call. loadLast (launcher, x2) runs the same steps without the guard and without the clamp, inside
//   `if (i >= end)`: identical whenever total >= rows; with total < rows it is only reachable from a window that
//   is not valid for the same row count (end < total), where the old code produced a negative start and
//   selectAtTop gives 0.
// - reload is gamelist reloadDirectoryAt. MenuTabs_reload has no "start = 0, end = min(n, rows)" step of its own:
//   it relies on buildRoot(next, 0, 0, 0), whose MenuTabs_clampWindow yields exactly that window, then runs the
//   same clamp. The test checks the two agree for every n, rows, keep.

// 0 .. min(total, rows). launcher pathToStack / openDirectory stale-restore fallback.
void ListWindow_fromTop(int total, int rows, int* start, int* end);
// max(0, total - rows) .. total. gamelist contentToBottom (and the List's wrap to the bottom in ListWindow_step).
void ListWindow_toBottom(int total, int rows, int* start, int* end);
// The window starts on sel, clamped to the list's end and to 0. See the reconciliation note above.
void ListWindow_selectAtTop(int total, int rows, int sel, int* start, int* end);
// Minimal scroll so sel shows: below the window it ends on sel, above it starts on sel, inside nothing moves.
// gamelist doRenameCollection (the caller keeps its `row >= 0 && row != selected` guard).
void ListWindow_reveal(int total, int rows, int sel, int* start, int* end);
// A rebuilt list: keep clamped into [0, total - 1] (0 when empty), window from the top, or ending on the selection
// when it would be below. Returns the clamped selection. Ignores the incoming window.
int ListWindow_reload(int total, int rows, int keep, int* start, int* end);
// One row up (delta < 0) or down (delta > 0), wrapping past an end only when may_wrap (a fresh press; a held key
// stops at the edge). Returns false when it stopped at the edge (or delta == 0 / total <= 0: no-op; the caller only
// steps a non-empty list), true when it stepped (sel may be unchanged: a one-row list wraps onto itself).
// gamelist GameList_handleInput UP/DOWN.
bool ListWindow_step(int total, int rows, int delta, bool may_wrap, int* sel, int* start, int* end);
// One page up (dir < 0) or down (dir > 0), stopping at the ends (no wrap). total <= 0 or dir == 0: no-op (the
// caller only pages a non-empty list). gamelist GameList_handleInput LEFT/RIGHT.
void ListWindow_page(int total, int rows, int dir, int* sel, int* start, int* end);

#endif
