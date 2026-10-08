// The Vertical orientation (§8f): render and D-pad input for the main-menu Carousel-Vertical (§8f.3) and the game
// lists' Backdrop-Vertical and Carousel-Vertical (§8f.4–6: the stack left of centre, the caption to its right; with
// Vertical alignment Right, mirrored: the stack right of centre, the caption, still left-aligned, to its left).
// Geometry and the d-pad's rules come from stack_model.c (dp, host-tested); the items are rowview.c's (rowview_shared.h):
// the frameless slots and the "N games" line, the Carousel's game tiles, the Backdrop's box art and placeholder box,
// and the game caption, all cached there per rest size (the caption per text), so nothing is rendered per frame but
// blits: no font, logo, tile or caption is made at a tweened size (an item changing size is its selected-size surface
// scaled, as on the row). A Backdrop-Vertical's picture (the crossfading screenshot layers with their 65% dim and the
// shade, its fade in and out) is the row's own, drawn under the body before RowView_render (RowView_renderPicture).
// As on the row, the main menu's stacks and a game list's Carousel and Backdrop stacks are drawn as GPU sprites
// (RowView_beginSprites: the same cached surfaces handed to the GPU, the Carousel's edge fade as black strips), so a
// slide draws nothing into the screen's body; under a context menu they are drawn the software way.

#include "ui_contextmenu.h"
#include "stackview.h"

#include "api.h"
#include "config.h"
#include "defines.h"
#include "ui_ease.h"
#include "ui_fade.h"
#include "ui_message.h"

#include "gamelist.h"
#include "imgloader.h" // screen
#include "launcher.h"
#include "list_window.h"
#include "menutabs.h"
#include "rowview.h"
#include "rowview_shared.h"
#include "shortcuts.h"
#include "stack_model.h"
#include "controller_art_model.h"

#include <math.h>
#include <string.h>

#define SCALE_EPS 0.004f // an item this close to its neighbour size is drawn 1:1 from that size's surface

// The stack's position (items): eases from pos_from to pos_to (STACK_SLIDE_MS, UI_easeStandard).
static float pos_from = 0, pos_to = 0;
static Tween slide_tw;
static bool slide_retargeted = false; // the slide began while another ran (a held D-pad's repeats)
// What the position belongs to: a change of any snaps it (the Directory serial and the tab generation, not the list).
static unsigned seen_top = 0; // 0 = none (forgotten)
static unsigned seen_gen = 0;
static int seen_n = -1, seen_screen_w = 0, seen_scale = 0, seen_kind = -1;
static bool snap_next = false; // UP from the tab row jumped to the last item: no slide across the list

///////////////////////////////////////
// Timing

static float slideProgress(void) {
	if (!slide_tw.active)
		return 1.0f;
	Uint32 elapsed = SDL_GetTicks() - slide_tw.start;
	return elapsed >= STACK_SLIDE_MS ? 1.0f : (float)elapsed / (float)STACK_SLIDE_MS;
}

static float currentPos(void) {
	if (!slide_tw.active)
		return pos_to;
	return pos_from + (pos_to - pos_from) * UI_easeStandard(slideProgress());
}

///////////////////////////////////////
// What the stack shows

static StackKind currentKind(void) {
	int tab = GameList_lookTab(); // the root tab, or a hidden tab's list pushed over it
	if (tab < 0) {				  // a game list in Carousel or Backdrop
		if (GameList_currentStyle() == MENU_STYLE_CAROUSEL)
			return STACK_GAME_CAROUSEL;
		if (Shortcuts_isInToolsFolder(top->path))
			return STACK_MAIN_TOOLS; // a Tools listing in Backdrop: its slots, centred, as the Tools tab's
		return STACK_GAME_BACKDROP;
	}
	switch (tab) {
	case MENU_TAB_CONSOLES:
		return STACK_MAIN_CONSOLES;
	case MENU_TAB_COLLECTIONS:
		return STACK_MAIN_COLLECTIONS;
	default:
		return STACK_MAIN_TOOLS;
	}
}

// The game lists' stacks: the side arrangement with the caption (the others centre their slots, captionless).
static bool gameStack(StackKind k) {
	return k == STACK_GAME_BACKDROP || k == STACK_GAME_CAROUSEL;
}

// The frame's geometry: the stack's sizes (dp), and for rowview's slots their px sizes and the selection's centre.
typedef struct {
	StackKind kind;
	StackSizes ss;
	StackSide side; // the game lists' side arrangement (dp)
	RowGeo g;
	int body_top, body_h; // px
	int vis_top, vis_h;	  // px: the body grown into a hidden page title's row and hint bar, where the items still show
	int cap_x, cap_w;	  // the side caption's column (px): from its left edge to the 24 dp right margin (Right: mirrored)
	bool right;			  // Vertical alignment Right (a game list): mirrored, the caption right-aligned
	float body_h_dp, sel_y_dp;
} StackGeo;

// Prefetch (StackView_prefetchStep) works for the last frame's stack: its geometry and selection, armed by each render
// and disarmed once everything ahead is built (or the stack stops drawing). It runs between frames, never forcing one.
static struct {
	bool armed;
	StackGeo sg;
	int n, sel;
} pf;

static void computeGeo(SDL_Surface* screen, StackKind kind, StackGeo* sg) {
	float pd = pxPerDp();
	int bar = barPx();
	bool game = gameStack(kind);
	sg->kind = kind;
	sg->body_top = bar;
	sg->body_h = screen->h - 2 * bar;
	sg->vis_top = CFG_getPageTitle() ? bar : 0;
	sg->vis_h = (CFG_getButtonHints() ? screen->h - bar : screen->h) - sg->vis_top;
	sg->body_h_dp = sg->body_h / pd;
	memset(&sg->side, 0, sizeof(sg->side));
	sg->ss = game ? Stack_gameSizes(kind, sg->body_h_dp, screen->w / pd, &sg->side) : Stack_mainSizes(kind, screen->w / pd);
	sg->sel_y_dp = Stack_selectionY(sg->body_h_dp, sg->ss.cap); // a game list's cap is 0: the body's middle

	RowGeo* g = &sg->g;
	memset(g, 0, sizeof(*g));
	switch (kind) {
	case STACK_GAME_CAROUSEL:
		g->kind = ROW_CAROUSEL;
		break;
	case STACK_GAME_BACKDROP:
		g->kind = ROW_BACKDROP_BOX;
		break;
	case STACK_MAIN_CONSOLES:
		g->kind = ROW_BACKDROP_LOGO;
		break;
	case STACK_MAIN_COLLECTIONS:
		g->kind = ROW_BACKDROP_COLL;
		break;
	default:
		g->kind = ROW_BACKDROP_TOOL;
		break;
	}
	g->cap = game ? CAP_GAME : CAP_NONE;
	g->vertical = true;
	g->sz = (RowSizes){sg->ss.item_w, sg->ss.item_h, sg->ss.scale, sg->ss.gap, 1.0f}; // unscaled (no small-screen f)
	g->cx = game ? Stack_round(sg->side.x * pd) : screen->w / 2;
	g->cy = sg->body_top + Stack_round(sg->sel_y_dp * pd);
	g->full_w = Stack_round(sg->ss.item_w * pd);
	g->full_h = Stack_round(sg->ss.item_h * pd);
	g->side_w = Stack_round(sg->ss.item_w * sg->ss.scale * pd);
	g->side_h = Stack_round(sg->ss.item_h * sg->ss.scale * pd);
	// the slot content scale, as the row's: min(1, slot_w / spec slot_w); 1 on both screens (the slots aren't capped).
	// A game tile or box art doesn't use it.
	float spec_w = kind == STACK_MAIN_CONSOLES ? ROWVIEW_LOGO_SLOT_W_SPEC : ROWVIEW_TOOL_SLOT_W_SPEC;
	g->k = game ? 1.0f : fminf(1.0f, sg->ss.item_w / spec_w);
	sg->cap_x = game ? Stack_capXPx(g->cx, g->full_w, pd) : 0; // from the drawn item (px), not rounded from dp
	sg->cap_w = game ? screen->w - Stack_round(sg->side.margin * pd) - sg->cap_x : 0;
	sg->right = game && CFG_getGameListVAlign() == MENU_VALIGN_RIGHT;
	if (sg->right) { // Vertical alignment Right: the arrangement mirrored, the caption right-aligned against the stack
		// Backdrop: the caption's left edge where Left's selected box is drawn (the placeholder's 0.72 case stands
		// for the box art, which varies per game), so its outer margin matches Left's
		int box_w = g->full_w < (int)(g->full_h * 0.72f + 0.5f) ? g->full_w : (int)(g->full_h * 0.72f + 0.5f);
		int start = kind == STACK_GAME_BACKDROP ? g->cx - box_w / 2 : 0;
		Stack_mirrorSide(screen->w, &g->cx, &sg->cap_x, &sg->cap_w, start);
	}
}

// Stack_visibleRange over the visible area (the body and any hidden title row or hint bar).
static void visibleRange(const StackGeo* sg, int n, float pos, int* first, int* last) {
	float pd = pxPerDp();
	float above = (sg->body_top - sg->vis_top) / pd;
	float below = (sg->vis_top + sg->vis_h - sg->body_top - sg->body_h) / pd;
	Stack_visibleRangeIn(&sg->ss, n, pos, sg->body_h_dp, above, below, first, last);
}

// An item's centre on screen (px).
static int itemCy(const StackGeo* sg, const StackItem* it) {
	return sg->body_top + Stack_round((sg->sel_y_dp + it->dy) * pxPerDp());
}

///////////////////////////////////////
// Drawing

// An item from its two rest-size surfaces: the neighbour's 1:1 at that size, else the selected one's scaled. A game
// list's Carousel tile darkens toward the black ground; its Backdrop box art, and every frameless slot, fade.
static void drawItem(SDL_Surface* screen, const StackGeo* sg, Entry* e, TileKind kind, const StackItem* it,
					 bool selected) {
	int cy = itemCy(sg, it);
	if (gameStack(sg->kind)) {
		RowView_drawGameItem(screen, &sg->g, e, kind, sg->g.cx, cy, it->scale, it->alpha, it->darken, it->d);
		return;
	}
	Uint8 a = (Uint8)(it->alpha * 255.0f + 0.5f);
	if (a == 0)
		return;
	if (RowView_drawConsoleLogo(screen, &sg->g, e, kind, sg->g.cx, cy, it->scale, a, selected))
		return;				   // a Consoles logo as a GPU sprite (sprite mode)
	if (!ContextMenu_isOpen()) // GPU sprites (every frameless stack): the full surface, scaled by the GPU
		RowView_blitItem(screen, RowView_slotItem(&sg->g, e, kind, false), sg->g.cx, cy, it->scale, a);
	else if (fabsf(it->scale - sg->ss.scale) < SCALE_EPS)
		RowView_blitItem(screen, RowView_slotItem(&sg->g, e, kind, true), sg->g.cx, cy, 1.0f, a);
	else
		RowView_blitItem(screen, RowView_slotItem(&sg->g, e, kind, false), sg->g.cx, cy, it->scale, a);
}

// The Consoles tab's controller art (docs/controller-art.md): a console's pad in 2.6 h x 1.7 h, centred on its logo
// and "N games" line (the block's centre sits half the gap and the line below the item's: the logo's own height
// cancels), and the extra room it takes above and below when it is the selection (dp). No pad: 0 room.
typedef struct {
	const char* id;
	PadSize box;  // dp
	float off;	  // the block centre below the item centre (dp)
	float up, dn; // the selection's extra reach (dp)
} StackPad;

static StackPad padFor(const StackGeo* sg, Entry* e, TileKind kind) {
	StackPad p = {RowView_padId(e, kind), Pad_stackBox(sg->ss.item_h), 0, 0, 0};
	const PadTableRow* row = p.id ? Pad_row(p.id) : NULL;
	if (!row) {
		p.id = NULL;
		return p;
	}
	p.off = (ROW_LOGO_COUNT_GAP_DP + RowView_countLineH(&sg->g) / pxPerDp()) / 2;
	PadSize drawn = Pad_fit(row->aspect, p.box.w, p.box.h);
	Stack_padExtra(&sg->ss, drawn.h, p.off, PAD_CLEAR_DP, &p.up, &p.dn);
	return p;
}

// The 20 dp fade to the black ground at the visible area's top and bottom, row by row. Across the screen's whole width
// at the body's edges (a game list's side caption is outside the stack's column); grown into a hidden page title's row
// or hint bar, only across the stack's column, so it never dims the status group. The plain-black stacks only: never
// over a Backdrop-Vertical's picture, which runs behind the edges.
typedef struct {
	int band;
	SDL_Rect where[2]; // top, bottom
} EdgeFade;

static EdgeFade edgeFadeGeo(SDL_Surface* screen, const StackGeo* sg) {
	EdgeFade f;
	float pd = pxPerDp();
	f.band = (int)ceilf(STACK_EDGE_FADE_DP * pd);
	if (f.band > sg->vis_h / 2)
		f.band = sg->vis_h / 2;
	int half = sg->g.full_w / 2 + NX_DP(16);
	int col_x = sg->g.cx - half < 0 ? 0 : sg->g.cx - half;
	int col_w = (sg->g.cx + half > screen->w ? screen->w : sg->g.cx + half) - col_x;
	bool top_grown = sg->vis_top<sg->body_top, bottom_grown = sg->vis_top + sg->vis_h> sg->body_top + sg->body_h;
	f.where[0] = (SDL_Rect){top_grown ? col_x : 0, sg->vis_top, top_grown ? col_w : screen->w, f.band};
	f.where[1] = (SDL_Rect){bottom_grown ? col_x : 0, sg->vis_top + sg->vis_h - f.band, bottom_grown ? col_w : screen->w,
							f.band};
	return f;
}

static void edgeFade(SDL_Surface* screen, const StackGeo* sg) {
	if (sg->kind == STACK_GAME_BACKDROP)
		return;
	float pd = pxPerDp();
	EdgeFade f = edgeFadeGeo(screen, sg);
	for (int r = 0; r < f.band; r++) {
		float a = Stack_edgeAlpha((r + 0.5f) / pd, sg->vis_h / pd, STACK_EDGE_FADE_DP);
		Uint8 dim = (Uint8)((1.0f - a) * 255.0f + 0.5f);
		if (dim == 0)
			continue;
		UI_dimRect(screen, &(SDL_Rect){f.where[0].x, f.where[0].y + r, f.where[0].w, 1}, dim);
		UI_dimRect(screen, &(SDL_Rect){f.where[1].x, f.where[1].y + f.band - 1 - r, f.where[1].w, 1}, dim);
	}
}

// edgeFade as two GPU sprites (sprite mode): black at the same per-row alpha, in a 1-px-wide strip per edge stretched
// across its width, made once per band and visible height.
static void edgeFadeSprites(SDL_Surface* screen, const StackGeo* sg) {
	static SDL_Surface* strips[2]; // top, bottom
	static int strip_band = -1, strip_vis = -1;
	if (sg->kind == STACK_GAME_BACKDROP)
		return; // never over the picture (as edgeFade)
	float pd = pxPerDp();
	EdgeFade f = edgeFadeGeo(screen, sg);
	if (f.band <= 0)
		return;
	if (f.band != strip_band || sg->vis_h != strip_vis) {
		for (int k = 0; k < 2; k++) {
			GFX_freeSurfaceAndTexture(strips[k]);
			strips[k] = SDL_CreateRGBSurfaceWithFormat(0, 1, f.band, 32, SDL_PIXELFORMAT_ARGB8888);
		}
		strip_band = f.band, strip_vis = sg->vis_h;
		for (int r = 0; r < f.band && strips[0] && strips[1]; r++) {
			float a = Stack_edgeAlpha((r + 0.5f) / pd, sg->vis_h / pd, STACK_EDGE_FADE_DP);
			Uint32 dim = (Uint32)((1.0f - a) * 255.0f + 0.5f);
			*((Uint32*)((Uint8*)strips[0]->pixels + r * strips[0]->pitch)) = dim << 24;
			*((Uint32*)((Uint8*)strips[1]->pixels + (f.band - 1 - r) * strips[1]->pitch)) = dim << 24;
		}
	}
	SDL_Rect clip = {0, sg->vis_top, screen->w, sg->vis_h};
	for (int k = 0; k < 2; k++) {
		SDL_Texture* t = strips[k] ? PLAT_textureForSurface(strips[k]) : NULL;
		if (t)
			PLAT_spriteAdd(t, NULL, &f.where[k], 255, &clip);
	}
}

// A list, tab, screen size, scale or kind change (or a forgotten list): start over with the stack snapped.
static bool syncList(SDL_Surface* screen, int n, int lastScreen, StackKind kind) {
	bool changed = seen_top == 0 || top->serial != seen_top || MenuTabs_generation() != seen_gen || n != seen_n ||
				   screen->w != seen_screen_w || (int)FIXED_SCALE != seen_scale || (int)kind != seen_kind ||
				   lastScreen != SCREEN_GAMELIST;
	if (!changed)
		return false;
	seen_top = top->serial;
	seen_gen = MenuTabs_generation();
	seen_n = n;
	seen_screen_w = screen->w;
	seen_scale = (int)FIXED_SCALE;
	seen_kind = (int)kind;
	return true;
}

// What the next step either way draws first: the neighbours at the selected size (they grow into the selection; a
// Carousel tile plain and lit), and at a neighbour's size the items that step brings into the body. Builds until done
// or the deadline; true when it stopped with more.
static bool prefetchItems(const StackGeo* sg, int n, int sel, Uint32 deadline) {
	static const struct {
		int di;
		bool side, lit;
	} jobs[] = {{1, false, false}, {1, false, true}, {-1, false, false}, {-1, false, true}, {2, true, false}, {-2, true, false}, {3, true, false}, {-3, true, false}, {4, true, false}, {-4, true, false}};
	bool skipped = false;
	// the selection's side caption first when the frame showed an older one of it (RowView_captionStale)
	if (RowView_captionStale() && gameStack(sg->kind) && sel >= 0 && sel < n) {
		if (!RowView_jobFits(deadline, PF_JOB_CAPTION))
			return true;
		Entry* e = top->entries->items[sel];
		bool done =
			RowView_prefetchSideCaption(&sg->g, e, RowView_kindFor(sel, e), sg->cap_w, sg->body_h, sg->right, deadline);
		RowView_captionRefreshed();
		if (!done)
			return true;
	}
	// a game stack's neighbours' side captions first (and in sprite mode their textures): the next step shows one at
	// once, and composing it in that frame made it late
	for (int di = 1; gameStack(sg->kind) && di >= -1; di -= 2) {
		int i = sel + di;
		if (i < 0 || i >= n)
			continue;
		if (!RowView_jobFits(deadline, PF_JOB_CAPTION)) {
			skipped = true; // the items still get their turn: the caption waits for a roomier frame
			break;
		}
		Entry* e = top->entries->items[i];
		if (!RowView_prefetchSideCaption(&sg->g, e, RowView_kindFor(i, e), sg->cap_w, sg->body_h, sg->right,
										 deadline)) {
			skipped = true;
			break;
		}
	}
	for (size_t j = 0; j < sizeof(jobs) / sizeof(jobs[0]); j++) {
		int i = sel + jobs[j].di;
		if (i < 0 || i >= n)
			continue;
		if (jobs[j].side) {
			int f, l;
			visibleRange(sg, n, (float)(sel + (jobs[j].di > 0 ? 1 : -1)), &f, &l);
			if (i < f || i > l)
				continue;
		}
		if (!RowView_jobFits(deadline, PF_JOB_ITEM))
			return true;
		Uint32 t0 = SDL_GetTicks();
		Entry* e = top->entries->items[i];
		RowView_prefetchItem(&sg->g, e, RowView_kindFor(i, e), jobs[j].side, jobs[j].lit);
		RowView_jobDone(PF_JOB_ITEM, t0);
	}
	if (sg->kind == STACK_MAIN_CONSOLES) { // the neighbours' controllers, which show as soon as a step starts
		PadSize box = Pad_stackBox(sg->ss.item_h);
		float pd = pxPerDp();
		for (int di = -1; di <= 1; di += 2) {
			int i = sel + di;
			if (i < 0 || i >= n)
				continue;
			if (RowView_pastDeadline(deadline))
				return true;
			Entry* e = top->entries->items[i];
			RowView_prefetchPad(e, RowView_kindFor(i, e), Stack_round(box.w * pd), Stack_round(box.h * pd));
		}
	}
	return skipped;
}

bool StackView_active(void) {
	// the main menu's Carousel, or a game list's Carousel or Backdrop (RowView_active: the style), stood on end
	return top && stack && RowView_active() && GameList_currentOrientation() == MENU_ORIENT_VERTICAL;
}

void StackView_render(SDL_Surface* screen, int lastScreen) {
	pf.armed = false;
	if (!screen || !top)
		return;
	StackKind kind = currentKind();
	int n = top->entries->count;
	RowView_syncKinds(n);
	bool snap = syncList(screen, n, lastScreen, kind) || snap_next;
	snap_next = false;
	if (n <= 0 || screen->h - 2 * barPx() <= 0) {
		slide_tw.active = false;
		pos_from = pos_to = 0;
		UI_renderCenteredMessage(screen, "Empty folder");
		return;
	}

	int sel = View_selectedIndex(n);
	// the slide toward the selection, retargeted from where the stack is now
	if (snap || !animationsOn()) {
		pos_from = pos_to = (float)sel;
		slide_tw.active = false;
	} else if ((float)sel != pos_to) {
		slide_retargeted = slideProgress() < 1.0f;
		pos_from = currentPos();
		pos_to = (float)sel;
		slide_tw.active = true;
		slide_tw.start = SDL_GetTicks();
	}
	float pos = currentPos();

	StackGeo sg;
	computeGeo(screen, kind, &sg);
	if (kind == STACK_MAIN_CONSOLES) {
		// the selection's pad room, easing linearly between the two items the position sits between
		int a = (int)floorf(pos), b = a + 1;
		float t = pos - a;
		StackPad pa = {0}, pb = {0};
		if (a >= 0 && a < n)
			pa = padFor(&sg, top->entries->items[a], RowView_kindFor(a, top->entries->items[a]));
		if (t > 0 && b < n)
			pb = padFor(&sg, top->entries->items[b], RowView_kindFor(b, top->entries->items[b]));
		sg.ss.extra_up = pa.up + (pb.up - pa.up) * t;
		sg.ss.extra_down = pa.dn + (pb.dn - pa.dn) * t;
	}

	if (kind == STACK_MAIN_CONSOLES) {
		PadSize box = Pad_stackBox(sg.ss.item_h);
		float pd = pxPerDp();
		RowView_warmConsoleArt(sg.g.full_w, sg.g.full_h, Stack_round(box.w * pd), Stack_round(box.h * pd), sel);
	}

	SDL_Rect prev_clip;
	SDL_GetClipRect(screen, &prev_clip);
	SDL_SetClipRect(screen, &(SDL_Rect){0, sg.vis_top, screen->w, sg.vis_h});

	// Consoles, Collections, Tools and a game list's Carousel and Backdrop: the pictures, counts, caption and edge fade
	// go to the GPU (RowView_beginSprites), the screen's body stays as RowView_render left it (not under a context menu:
	// it draws over the body on the screen, under the sprites)
	bool sprites = (!gameStack(kind) && !ContextMenu_isOpen()) || (gameStack(kind) && RowView_gameSprites());
	RowView_beginSprites(sprites);

	// far to near (the largest d first), so the nearer item lands on top where a grown name reaches a neighbour
	int first, last;
	visibleRange(&sg, n, pos, &first, &last);
	int order[16], count = 0;
	for (int i = first; i <= last && count < 16; i++)
		order[count++] = i;
	for (int a = 0; a < count; a++) {
		for (int b = a + 1; b < count; b++) {
			if (fabsf(order[b] - pos) > fabsf(order[a] - pos)) {
				int t = order[a];
				order[a] = order[b];
				order[b] = t;
			}
		}
	}
	for (int j = 0; j < count; j++) {
		int i = order[j];
		StackItem it = Stack_item(&sg.ss, (float)i, pos);
		if (!it.visible)
			continue;
		Entry* e = top->entries->items[i];
		TileKind k = RowView_kindFor(i, e);
		if (kind == STACK_MAIN_CONSOLES && it.d < 1.0f) { // the focused console's controller, under its logo
			StackPad p = padFor(&sg, e, k);
			if (p.id) {
				float pd = pxPerDp();
				RowView_drawPad(screen, e, k, Stack_round(p.box.w * pd), Stack_round(p.box.h * pd), sg.g.cx,
								itemCy(&sg, &it) + Stack_round(p.off * it.scale * pd), it.scale, it.d);
			}
		}
		drawItem(screen, &sg, e, k, &it, i == sel);
	}

	// "N games". Consoles': on its own item, under that logo as drawn at the item's live place and scale, on the items
	// within a step of the position (the outgoing selection's fading out as the incoming one's fades in), so a logo
	// sliding through the selection never runs under a count left in the slot (device fix round 2). Collections': on
	// the selected item itself (RowView_drawCount, fading in); nothing in a game list. A game list's caption sits
	// beside the stack, centred on the selection (its game info requested last: GameInfo's queue keeps the latest).
	Entry* e = top->entries->items[sel];
	if (kind == STACK_MAIN_CONSOLES) {
		for (int i = first; i <= last; i++) {
			StackItem it = Stack_item(&sg.ss, (float)i, pos);
			if (it.d >= 1.0f)
				continue;
			Entry* ie = top->entries->items[i];
			RowView_drawItemCount(screen, &sg.g, ie, RowView_kindFor(i, ie), sg.g.cx, itemCy(&sg, &it), it.scale,
								  it.d);
		}
	} else {
		StackItem sit = Stack_item(&sg.ss, (float)sel, pos);
		RowPlace at = {0, itemCy(&sg, &sit) - sg.g.cy, sit.scale, sit.alpha, sit.visible};
		RowView_drawCount(screen, &sg.g, e, RowView_kindFor(sel, e), sel, &at, snap);
	}
	if (gameStack(kind))
		RowView_drawSideCaption(screen, &sg.g, e, RowView_kindFor(sel, e), sg.cap_x, sg.cap_w, sg.g.cy, sg.body_top,
								sg.body_h, sg.right);

	if (sprites)
		edgeFadeSprites(screen, &sg);
	else
		edgeFade(screen, &sg);
	RowView_beginSprites(false);
	SDL_SetClipRect(screen, &prev_clip);

	// what's ahead of this stack is built between frames (StackView_prefetchStep), for this geometry and selection
	pf.armed = true;
	pf.sg = sg;
	pf.n = n;
	pf.sel = sel;
}

bool StackView_prefetchStep(Uint32 deadline) {
	if (!pf.armed)
		return false;
	// the stack the geometry was worked out for is gone (a new list, tab, size, scale or kind not drawn yet, a moved
	// selection, or a forgotten stack): nothing until the next render re-arms it
	if (!StackView_active() || !screen || seen_top == 0 || top->serial != seen_top ||
		MenuTabs_generation() != seen_gen || top->entries->count != pf.n || screen->w != seen_screen_w ||
		(int)FIXED_SCALE != seen_scale || (int)currentKind() != seen_kind || View_selectedIndex(pf.n) != pf.sel) {
		pf.armed = false;
		return false;
	}
	// as the row's (RowView_prefetchStep): not while the picture is busy, nor through the first part of a single
	// step's slide; a held D-pad's retargeted slide builds for its target
	if (RowView_prefetchBlocked() || (slide_tw.active && !slide_retargeted && slideProgress() < PREFETCH_SLIDE_SHARE))
		return true;
	if (prefetchItems(&pf.sg, pf.n, pf.sel, deadline))
		return true;
	pf.armed = false;
	return false;
}

///////////////////////////////////////
// Input

// The selection moves to i; the List window follows (the selection at its top, clamped), as the row keeps it.
static void selectItem(int i) {
	int n = top->entries->count;
	top->selected = i;
	ListWindow_selectAtTop(n, GameList_rowCount(), i, &top->start, &top->end);
}

bool StackView_handleInput(bool* dirty) {
	static const struct {
		int btn;
		StackKey key;
	} keys[] = {{BTN_UP, STACK_KEY_UP}, {BTN_DOWN, STACK_KEY_DOWN}, {BTN_LEFT, STACK_KEY_LEFT}, {BTN_RIGHT, STACK_KEY_RIGHT}};
	for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
		if (!PAD_justRepeated(keys[k].btn))
			continue;
		int n = top->entries->count;
		StackNav nav = Stack_navigate(n, n > 0 ? View_selectedIndex(n) : 0, keys[k].key, PAD_justPressed(keys[k].btn),
									  stack->count == 1);
		switch (nav.action) {
		case STACK_NAV_MOVE:
			selectItem(nav.sel);
			*dirty = true;
			break;
		case STACK_NAV_TAB_ROW:
			MenuTabs_setFocused(true);
			*dirty = true;
			break;
		case STACK_NAV_SWITCH_TAB:
			GameList_switchTab(nav.dir, dirty);
			break;
		case STACK_NAV_NONE:
			break; // a held key at an end, or LEFT/RIGHT held: still the stack's (the List's moves don't run)
		}
		return true;
	}
	return false;
}

void StackView_focusBottom(void) {
	int n = top ? top->entries->count : 0;
	if (n <= 0)
		return;
	selectItem(Stack_fromTabRow(n));
	snap_next = true;
}

bool StackView_animating(void) {
	bool sliding = slide_tw.active;
	if (sliding && SDL_GetTicks() - slide_tw.start >= STACK_SLIDE_MS) {
		slide_tw.active = false; // one settled frame as it clears
		pos_from = pos_to;
	}
	return sliding;
}

void StackView_forget(void) {
	seen_top = 0;
	slide_tw.active = false;
	pos_from = pos_to;
	snap_next = false;
	pf.armed = false;
}
