// ListWindow (nextui/list_window.c) against the inline blocks it replaces. Each legacy_* below is the old caller's
// block pasted as-is (only wrapped in a function over a stand-in Directory); the test checks equality exhaustively.
#include "../../nextui/list_window.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
	int selected, start, end;
} Dir;

static long checks = 0;

/////////////////////////////// legacy copies

// gamelist.c reloadDirectoryAt (fresh = the rebuilt Directory, n = its count, rc = GameList_rowCountAt)
static void legacy_reloadDirectoryAt(Dir* fresh, int n, int rc, int keep_selected) {
	int sel = keep_selected;
	if (sel >= n)
		sel = n > 0 ? n - 1 : 0;
	if (sel < 0)
		sel = 0;
	fresh->selected = sel;

	fresh->start = 0;
	fresh->end = (n < rc) ? n : rc;
	if (sel >= fresh->end && n > rc) {
		fresh->end = sel + 1;
		fresh->start = fresh->end - rc;
	}
}

// menutabs_model.c MenuTabs_clampWindow (what buildRoot(next, 0, 0, 0) runs)
static void legacy_clampWindow(int count, int rows, int* selected, int* start, int* end) {
	int sel = *selected, first = *start, last = *end;
	if (count < 0)
		count = 0;
	if (rows < 1)
		rows = 1;
	if (sel < 0 || sel >= count || last > count || first < 0 || first > sel || (last && sel >= last)) {
		sel = 0;
		first = 0;
		last = 0;
	}
	int visible = count < rows ? count : rows;
	if (last == 0 || last - first != visible) {
		if (sel >= first + visible)
			first = sel - visible + 1;
		if (first > count - visible)
			first = count - visible;
		if (first < 0)
			first = 0;
		last = first + visible;
	}
	*selected = sel;
	*start = first;
	*end = last;
}

// menutabs.c MenuTabs_reload: buildRoot(next, 0, 0, 0) then the clamp
static void legacy_MenuTabs_reload(Dir* fresh, int n, int rc, int sel) {
	int s0 = 0, st0 = 0, e0 = 0;
	legacy_clampWindow(n, rc, &s0, &st0, &e0); // buildRoot
	fresh->selected = s0;
	fresh->start = st0;
	fresh->end = e0;
	if (sel >= n)
		sel = n > 0 ? n - 1 : 0;
	fresh->selected = sel < 0 ? 0 : sel;
	if (fresh->selected >= fresh->end && n > rc) { // same windowing as reloadDirectoryAt
		fresh->end = fresh->selected + 1;
		fresh->start = fresh->end - rc;
	}
}

// gamelist.c doRenameCollection (inside `row >= 0 && row != root->selected`)
static void legacy_doRenameCollection(Dir* root, int total, int rc, int row) {
	root->selected = row;
	if (row >= root->end) { // below the window: it ends on the row
		root->end = row + 1;
		root->start = root->end > rc ? root->end - rc : 0;
	} else if (row < root->start) { // above it: it starts on the row
		root->start = row;
		root->end = row + rc < total ? row + rc : total;
	}
}

// gamelist.c contentToBottom (List branch)
static void legacy_contentToBottom(Dir* top, int total, int rc) {
	top->selected = total - 1;
	top->start = total > rc ? total - rc : 0;
	top->end = total;
}

// launcher.c pathToStack (root_dir / merged / dir) and openDirectory's stale fallback
static void legacy_pathToStack(Dir* dir, int count, int rows) {
	dir->start = 0;
	dir->end = (count < rows) ? count : rows;
}
static void legacy_openDirectoryFallback(Dir* top, int count, int rc) {
	top->selected = 0;
	top->start = 0;
	top->end = (count < rc) ? count : rc;
}

// gamelist.c GameList_handleInput UP/DOWN (justPressed = PAD_justPressed of that key)
static void legacy_upDown(Dir* top, int total, int row_count, bool up, bool justPressed) {
	int selected = top->selected;
	if (up) {
		if (selected == 0 && !justPressed) {
		} else {
			selected -= 1;
			if (selected < 0) {
				selected = total - 1;
				int start = total - row_count;
				top->start = (start < 0) ? 0 : start;
				top->end = total;
			} else if (selected < top->start) {
				top->start -= 1;
				top->end -= 1;
			}
		}
	} else {
		if (selected == total - 1 && !justPressed) {
		} else {
			selected += 1;
			if (selected >= total) {
				selected = 0;
				top->start = 0;
				top->end = (total < row_count) ? total : row_count;
			} else if (selected >= top->end) {
				top->start += 1;
				top->end += 1;
			}
		}
	}
	top->selected = selected;
}

// gamelist.c GameList_handleInput LEFT/RIGHT page jump
static void legacy_page(Dir* top, int total, int row_count, bool left) {
	int selected = top->selected;
	if (left) {
		selected -= row_count;
		if (selected < 0) {
			selected = 0;
			top->start = 0;
			top->end = (total < row_count) ? total : row_count;
		} else if (selected < top->start) {
			top->start -= row_count;
			if (top->start < 0)
				top->start = 0;
			top->end = top->start + row_count;
		}
	} else {
		selected += row_count;
		if (selected >= total) {
			selected = total - 1;
			int start = total - row_count;
			top->start = (start < 0) ? 0 : start;
			top->end = total;
		} else if (selected >= top->end) {
			top->end += row_count;
			if (top->end > total)
				top->end = total;
			top->start = top->end - row_count;
		}
	}
	top->selected = selected;
}

// gamelist.c GameList_handleInput L1/R1 letter jump (selected = top->alphas.items[i])
static void legacy_letterJump(Dir* top, int total, int row_count, int selected) {
	top->selected = selected;
	if (total > row_count) {
		top->start = selected;
		top->end = top->start + row_count;
		if (top->end > total)
			top->end = total;
		top->start = top->end - row_count;
	}
}

// launcher.c loadLast (both copies are identical; lrc = GameList_rowCount())
static void legacy_loadLast(Dir* top, int count, int lrc, int i) {
	top->selected = i;
	if (i >= top->end) {
		top->start = i;
		top->end = top->start + lrc;
		if (top->end > count) {
			top->end = count;
			top->start = top->end - lrc;
		}
	}
}

// gridview.c selectTile (rowview.c LEFT/RIGHT and stackview.c selectItem are the same lines)
static void legacy_selectTile(Dir* top, int n, int rows, int index) {
	top->selected = index;
	top->start = index;
	top->end = top->start + rows < n ? top->start + rows : n;
	if (top->end - top->start < rows) {
		top->start = top->end - rows;
		if (top->start < 0)
			top->start = 0;
	}
}

/////////////////////////////// helpers

static int imin(int a, int b) {
	return a < b ? a : b;
}
static bool same(Dir a, Dir b) {
	checks++;
	return a.selected == b.selected && a.start == b.start && a.end == b.end;
}
static bool validFor(Dir d, int total, int rows) {
	return d.start >= 0 && d.end == d.start + imin(total, rows) && d.end <= total;
}
static bool shows(Dir d, int total, int rows) {
	return validFor(d, total, rows) && (total == 0 || (d.selected >= d.start && d.selected < d.end));
}

#define MAX_TOTAL 12
#define MAX_ROWS 8

/////////////////////////////// exhaustive equality

static void exhaustive(void) {
	for (int total = 0; total <= MAX_TOTAL; total++) {
		for (int rows = 1; rows <= MAX_ROWS; rows++) {
			// fromTop / toBottom
			{
				Dir a = {7, 3, 9}, b = a;
				legacy_pathToStack(&a, total, rows);
				ListWindow_fromTop(total, rows, &b.start, &b.end);
				assert(same(a, b) && validFor(b, total, rows));
				a = (Dir){7, 3, 9};
				b = a;
				legacy_openDirectoryFallback(&a, total, rows);
				b.selected = 0;
				ListWindow_fromTop(total, rows, &b.start, &b.end);
				assert(same(a, b));
				if (total > 0) {
					a = (Dir){0, 0, 0};
					b = a;
					legacy_contentToBottom(&a, total, rows);
					b.selected = total - 1;
					ListWindow_toBottom(total, rows, &b.start, &b.end);
					assert(same(a, b) && shows(b, total, rows));
				}
			}
			// reload: keep anywhere, incoming window ignored; MenuTabs_reload agrees with reloadDirectoryAt
			for (int keep = -3; keep <= total + 3; keep++) {
				Dir a = {-1, -1, -1}, m = a, b = {0, 5, 2};
				legacy_reloadDirectoryAt(&a, total, rows, keep);
				legacy_MenuTabs_reload(&m, total, rows, keep);
				b.selected = ListWindow_reload(total, rows, keep, &b.start, &b.end);
				assert(same(a, b) && same(m, b) && shows(b, total, rows));
			}
			// window-taking ops: every 0 <= start <= end <= total (valid or not), every sel in the list
			for (int start = 0; start <= total; start++) {
				for (int end = start; end <= total; end++) {
					bool valid = end - start == imin(total, rows);
					for (int sel = 0; sel < total; sel++) {
						Dir w = {sel, start, end};
						bool inside = valid && sel >= start && sel < end;
						// selectAtTop vs the three views (all inputs)
						Dir a = w, b = w;
						legacy_selectTile(&a, total, rows, sel);
						ListWindow_selectAtTop(total, rows, sel, &b.start, &b.end);
						assert(same(a, b) && shows(b, total, rows));
						// letter jump: identical with the caller's `total > rows` guard kept
						a = w;
						legacy_letterJump(&a, total, rows, sel);
						Dir c = w;
						if (total > rows)
							ListWindow_selectAtTop(total, rows, sel, &c.start, &c.end);
						assert(same(a, c));
						if (valid) // ... and even without the guard on a valid window
							assert(same(a, b));
						// loadLast inside `if (i >= end)`: identical when total >= rows or the window is valid
						a = w;
						legacy_loadLast(&a, total, rows, sel);
						c = w;
						if (sel >= c.end)
							ListWindow_selectAtTop(total, rows, sel, &c.start, &c.end);
						if (total >= rows || valid)
							assert(same(a, c));
						// reveal vs doRenameCollection (all inputs)
						for (int row = 0; row < total; row++) {
							a = w;
							legacy_doRenameCollection(&a, total, rows, row);
							c = w;
							c.selected = row;
							ListWindow_reveal(total, rows, row, &c.start, &c.end);
							assert(same(a, c));
							if (valid)
								assert(shows(c, total, rows));
						}
						// step, both directions, fresh press or held (all inputs)
						for (int up = 0; up <= 1; up++) {
							for (int fresh = 0; fresh <= 1; fresh++) {
								a = w;
								legacy_upDown(&a, total, rows, up, fresh);
								c = w;
								bool moved = ListWindow_step(total, rows, up ? -1 : 1, fresh, &c.selected, &c.start,
															 &c.end);
								assert(same(a, c));
								bool at_edge = up ? sel == 0 : sel == total - 1;
								assert(moved == !(at_edge && !fresh));
								if (inside)
									assert(shows(c, total, rows));
							}
						}
						// page, both directions (all inputs)
						for (int left = 0; left <= 1; left++) {
							a = w;
							legacy_page(&a, total, rows, left);
							c = w;
							ListWindow_page(total, rows, left ? -1 : 1, &c.selected, &c.start, &c.end);
							assert(same(a, c));
							if (inside)
								assert(shows(c, total, rows));
						}
					}
				}
			}
			// empty list: step / page are no-ops (the caller guards total > 0)
			if (total == 0) {
				Dir c = {0, 0, 0};
				assert(!ListWindow_step(0, rows, -1, true, &c.selected, &c.start, &c.end));
				ListWindow_page(0, rows, 1, &c.selected, &c.start, &c.end);
				assert(same(c, (Dir){0, 0, 0}));
			}
		}
	}
}

/////////////////////////////// named cases

static void named(void) {
	int sel, s, e;
	// wrap up from the top (fresh press): last row, window at the bottom
	sel = 0, s = 0, e = 5;
	assert(ListWindow_step(12, 5, -1, true, &sel, &s, &e) && sel == 11 && s == 7 && e == 12);
	// wrap down from the bottom: first row, window at the top
	assert(ListWindow_step(12, 5, 1, true, &sel, &s, &e) && sel == 0 && s == 0 && e == 5);
	// short list wrap up: window stays 0..n
	sel = 0, s = 0, e = 3;
	assert(ListWindow_step(3, 5, -1, true, &sel, &s, &e) && sel == 2 && s == 0 && e == 3);
	// held at an edge: stops, nothing moves
	sel = 0, s = 0, e = 5;
	assert(!ListWindow_step(12, 5, -1, false, &sel, &s, &e) && sel == 0 && s == 0 && e == 5);
	sel = 11, s = 7, e = 12;
	assert(!ListWindow_step(12, 5, 1, false, &sel, &s, &e) && sel == 11 && s == 7 && e == 12);
	// held mid-list: still steps (repeat), scrolling by one past the window
	sel = 4, s = 0, e = 5;
	assert(ListWindow_step(12, 5, 1, false, &sel, &s, &e) && sel == 5 && s == 1 && e == 6);
	sel = 1, s = 1, e = 6;
	assert(ListWindow_step(12, 5, -1, false, &sel, &s, &e) && sel == 0 && s == 0 && e == 5);
	// page at the top / bottom ends: clamp, no wrap
	sel = 2, s = 0, e = 5;
	ListWindow_page(12, 5, -1, &sel, &s, &e);
	assert(sel == 0 && s == 0 && e == 5);
	sel = 9, s = 7, e = 12;
	ListWindow_page(12, 5, 1, &sel, &s, &e);
	assert(sel == 11 && s == 7 && e == 12);
	// page mid-list: down a page, window moves by a page (clamped to the end), then back up
	sel = 4, s = 0, e = 5;
	ListWindow_page(12, 5, 1, &sel, &s, &e);
	assert(sel == 9 && s == 5 && e == 10);
	ListWindow_page(12, 5, 1, &sel, &s, &e);
	assert(sel == 11 && s == 7 && e == 12);
	sel = 8, s = 5, e = 10;
	ListWindow_page(12, 5, -1, &sel, &s, &e);
	assert(sel == 3 && s == 0 && e == 5);
	// page past a window that can't move a whole page: it ends on the list's end
	sel = 0, s = 0, e = 8;
	ListWindow_page(12, 8, 1, &sel, &s, &e);
	assert(sel == 8 && s == 4 && e == 12);
	// letter jump: the letter's first row at the top; near the end the window clamps to the end
	s = 0, e = 5;
	ListWindow_selectAtTop(12, 5, 6, &s, &e);
	assert(s == 6 && e == 11);
	ListWindow_selectAtTop(12, 5, 10, &s, &e);
	assert(s == 7 && e == 12);
	ListWindow_selectAtTop(3, 5, 2, &s, &e); // short list: whole list
	assert(s == 0 && e == 3);
	// reload clamp: keep past the end -> last row, window ends on it
	assert(ListWindow_reload(10, 4, 15, &s, &e) == 9 && s == 6 && e == 10);
	// keep inside the first page: window from the top
	assert(ListWindow_reload(10, 4, 2, &s, &e) == 2 && s == 0 && e == 4);
	// fewer entries than rows
	assert(ListWindow_reload(3, 8, 5, &s, &e) == 2 && s == 0 && e == 3);
	// empty list
	assert(ListWindow_reload(0, 8, 4, &s, &e) == 0 && s == 0 && e == 0);
	assert(ListWindow_reload(0, 8, -2, &s, &e) == 0 && s == 0 && e == 0);
	// reveal: below ends on the row, above starts on it, inside unchanged
	s = 0, e = 4;
	ListWindow_reveal(10, 4, 7, &s, &e);
	assert(s == 4 && e == 8);
	ListWindow_reveal(10, 4, 1, &s, &e);
	assert(s == 1 && e == 5);
	ListWindow_reveal(10, 4, 3, &s, &e);
	assert(s == 1 && e == 5);
	// toBottom / fromTop
	ListWindow_toBottom(10, 4, &s, &e);
	assert(s == 6 && e == 10);
	ListWindow_toBottom(3, 4, &s, &e);
	assert(s == 0 && e == 3);
	ListWindow_fromTop(10, 4, &s, &e);
	assert(s == 0 && e == 4);
	ListWindow_fromTop(3, 4, &s, &e);
	assert(s == 0 && e == 3);
	// rows < 1 is treated as 1
	ListWindow_fromTop(10, 0, &s, &e);
	assert(s == 0 && e == 1);
	// the one documented divergence: loadLast with total < rows on a window not valid for those rows (end < total)
	// gave a negative start; selectAtTop clamps it to 0. Unreachable with the row count that built the window.
	Dir old = {0, 0, 1};
	legacy_loadLast(&old, 3, 5, 2);
	assert(old.start == -2 && old.end == 3);
	s = 0, e = 1;
	ListWindow_selectAtTop(3, 5, 2, &s, &e);
	assert(s == 0 && e == 3);
}

int main(void) {
	exhaustive();
	named();
	printf("test_list_window: ok (%ld comparisons)\n", checks);
	return 0;
}
