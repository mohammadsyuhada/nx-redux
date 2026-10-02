// List window rules, SDL-free so they can be host-tested (common/tests/test_list_window.c). Each body is a verbatim
// transcription of the inline block named above it (only the variable names changed: top->start -> *start, ...).

#include "list_window.h"

static int clampRows(int rows) {
	return rows < 1 ? 1 : rows;
}

// launcher.c pathToStack (root, merged, pushed dirs) and openDirectory's stale-restore fallback
void ListWindow_fromTop(int total, int rows, int* start, int* end) {
	rows = clampRows(rows);
	*start = 0;
	*end = (total < rows) ? total : rows;
}

// gamelist.c contentToBottom (List branch); same as GameList_handleInput's UP wrap
void ListWindow_toBottom(int total, int rows, int* start, int* end) {
	rows = clampRows(rows);
	*start = total > rows ? total - rows : 0;
	*end = total;
}

// gridview.c selectTile / rowview.c LEFT-RIGHT / stackview.c selectItem (the three are identical)
void ListWindow_selectAtTop(int total, int rows, int sel, int* start, int* end) {
	rows = clampRows(rows);
	*start = sel;
	*end = *start + rows < total ? *start + rows : total;
	if (*end - *start < rows) {
		*start = *end - rows;
		if (*start < 0)
			*start = 0;
	}
}

// gamelist.c doRenameCollection (the re-windowing after MenuTabs_reload)
void ListWindow_reveal(int total, int rows, int sel, int* start, int* end) {
	rows = clampRows(rows);
	if (sel >= *end) { // below the window: it ends on the row
		*end = sel + 1;
		*start = *end > rows ? *end - rows : 0;
	} else if (sel < *start) { // above it: it starts on the row
		*start = sel;
		*end = sel + rows < total ? sel + rows : total;
	}
}

// gamelist.c reloadDirectoryAt (MenuTabs_reload is the same, see list_window.h)
int ListWindow_reload(int total, int rows, int keep, int* start, int* end) {
	rows = clampRows(rows);
	int n = total;
	int sel = keep;
	if (sel >= n)
		sel = n > 0 ? n - 1 : 0;
	if (sel < 0)
		sel = 0;

	*start = 0;
	*end = (n < rows) ? n : rows;
	if (sel >= *end && n > rows) {
		*end = sel + 1;
		*start = *end - rows;
	}
	return sel;
}

// gamelist.c GameList_handleInput, BTN_UP / BTN_DOWN (may_wrap = PAD_justPressed)
bool ListWindow_step(int total, int rows, int delta, bool may_wrap, int* sel, int* start, int* end) {
	rows = clampRows(rows);
	if (total <= 0 || delta == 0)
		return false; // the caller's `total > 0` guard
	int selected = *sel;
	if (delta < 0) {
		if (selected == 0 && !may_wrap)
			return false;
		selected -= 1;
		if (selected < 0) {
			selected = total - 1;
			int s = total - rows;
			*start = (s < 0) ? 0 : s;
			*end = total;
		} else if (selected < *start) {
			*start -= 1;
			*end -= 1;
		}
	} else {
		if (selected == total - 1 && !may_wrap)
			return false;
		selected += 1;
		if (selected >= total) {
			selected = 0;
			*start = 0;
			*end = (total < rows) ? total : rows;
		} else if (selected >= *end) {
			*start += 1;
			*end += 1;
		}
	}
	*sel = selected;
	return true;
}

// gamelist.c GameList_handleInput, BTN_LEFT / BTN_RIGHT page jump (game lists only)
void ListWindow_page(int total, int rows, int dir, int* sel, int* start, int* end) {
	rows = clampRows(rows);
	if (total <= 0 || dir == 0)
		return; // the caller's `total > 0` guard
	int selected = *sel;
	if (dir < 0) {
		selected -= rows;
		if (selected < 0) {
			selected = 0;
			*start = 0;
			*end = (total < rows) ? total : rows;
		} else if (selected < *start) {
			*start -= rows;
			if (*start < 0)
				*start = 0;
			*end = *start + rows;
		}
	} else {
		selected += rows;
		if (selected >= total) {
			selected = total - 1;
			int s = total - rows;
			*start = (s < 0) ? 0 : s;
			*end = total;
		} else if (selected >= *end) {
			*end += rows;
			if (*end > total)
				*end = total;
			*start = *end - rows;
		}
	}
	*sel = selected;
}
