#include "home_list_layout.h"

int HomeList_count(int ngames, int ntools) {
	return 1 + (ngames > 0 ? ngames : 0) + (ntools > 0 ? ntools : 0);
}

HomeListItem HomeList_item(int i, int ngames, int ntools) {
	if (ngames < 0)
		ngames = 0;
	if (ntools < 0)
		ntools = 0;
	i = HomeList_clampSel(i, ngames, ntools);
	if (i == 0)
		return (HomeListItem){HOMELIST_CONTINUE, 0};
	if (i <= ngames)
		return (HomeListItem){HOMELIST_GAME, i - 1};
	return (HomeListItem){HOMELIST_TOOL, i - 1 - ngames};
}

int HomeList_clampSel(int sel, int ngames, int ntools) {
	int n = HomeList_count(ngames, ntools);
	if (sel >= n)
		sel = n - 1;
	return sel < 0 ? 0 : sel;
}

void HomeList_compute(int strip_lines, const HomeListOpts* o, HomeListGeom* out) {
	int lines = strip_lines < 0 ? 0 : strip_lines > 2 ? 2
													  : strip_lines;
	out->strip_lines = lines;
	out->strip_base[0] = out->strip_base[1] = 0;
	if (lines == 0) {
		out->list_top = o->list_top_plain;
		return;
	}
	out->strip_base[0] = o->bar + o->strip_gap + o->asc;
	out->strip_base[1] = out->strip_base[0] + o->line_step;
	out->list_top = out->strip_base[lines - 1] + o->desc + o->list_gap;
}

// 2 / 16 and 8 / 16 of the tag's line, its gap 10 / 16 of it (the mockup's px on its 16 px line)
HomeListTag HomeList_tag(int th, int text_w) {
	HomeListTag t;
	t.pad_y = (th * 2 + 8) / 16;
	t.pad_x = (th * 8 + 8) / 16;
	t.gap = (th * 10 + 8) / 16;
	t.w = text_w + 2 * t.pad_x;
	t.h = th + 2 * t.pad_y;
	return t;
}
