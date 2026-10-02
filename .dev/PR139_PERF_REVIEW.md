# PR #139 (`main-menu-tabs`) — performance, responsiveness and refactor review

Written 2026-10-02 23:55 +08 by a Claude Code session (Fable 5.1 orchestrating two Opus review
agents + a `/code-review high` correctness pass). This file is the hand-off for any later session
that picks up the follow-up work. Everything below is **read-only review output** — nothing in the
branch had been changed when this file was first written. The "Execution ledger" at the bottom is
the place to record what has since been done.

## Context

| Item | Value |
|---|---|
| Branch | `main-menu-tabs`, PR [#139](https://github.com/…/pull/139) "Main menu redesign: Home, tabs and List / Grid / Carousel / Backdrop layouts" |
| Worktree | `/Users/mohammadsyuhada/Work/Personal/nx-redux/.claude/worktrees/main-menu-tabs` |
| Branch tip at review time | `0c2c5d9a` (25 commits ahead of `main` @ `ade500ff`) |
| Diff size | 366 files, +20 964 / −1 986 |
| Targets | TrimUI Brick (tg5040, 1024×768, Cortex-A53) and Smart Pro S (tg5050, 1280×720). 60 fps, SDL2, exFAT SD card (slow `stat()`/`readdir`). |
| Main loop | `workspace/all/nextui/nextui.c`: `PAD_poll` → input → maybe rebuild list → render → `GFX_flip`; sleeps when idle; a `dirty` flag drives redraws |
| Review goal | A frontend that is fast and responsive: input latency, dropped frames, SD-card I/O on the input path, startup/return-from-game cost, cache correctness, then refactor opportunities |

All paths below are relative to `workspace/all/` in the worktree unless stated otherwise. Line numbers
are as of `0c2c5d9a`; they will drift as fixes land.

### Largest changed/new source files

```
nextui/rowview.c          2042 (new)  Carousel / Backdrop row view
nextui/home.c             1528 (new)  Home tab
nextui/gamelist.c        +1288        List view + input + context actions
nextui/tiles.c             907 (new)  tile composition + caches
nextui/gridview.c          795 (new)  Grid view
nextui/menutabs.c          717 (new)  tab row, per-tab root Directory
nextui/homeart.c           600 (new)  background art decode/scale worker
common/ui/ui_fade.c        415 (new)  NEON/scalar fade + blend kernels
nextui/stackview.c         402 (new)  vertical stacks
nextui/infoband.c          362 (new)
nextui/collcount.c         254 (new)  collection count worker + cache file
nextui/home_layout.c, grid_layout.c, row_model.c, stack_model.c, home_stats.c, gameinfo.c
common/ui/ui_font.c        205 (new)  TTF font LRU
common/ui/ui_list.c, ui_menubar.c, api.c, config.c, settings/settings.c (Layouts page)
common/tests/test_{row_model,stack_model,grid_layout,infoband_layout,list_layout,title_fit,
                   menutabs_model,home_layout,homeart_model,caption_fit,menustyle,menu_transition}.c
```

### Commits on the branch (newest first)

```
0c2c5d9a feat(ui): keep the tabs, page titles, hint bar, status bar and Grid gap at 1x on a larger UI scale; …
668ca8da feat(nextui): game-list Grid tiles at 1.5x width
d28e69cd feat(nextui): main menu art, sizing and colour polish, retire the mix and game-art settings, …
789d99c3 chore(skeleton): drop the Artbook Roms backgrounds and their credit
360222eb feat(res): re-export console logos at 1024x256, add the 3DS logo …
7b63fa46 feat(nextui): larger off-white Consoles carousel logos, dimmer neighbours and a title-only placeholder box
17f88c85 feat(nextui): area-average menu art and neighbour scaling so logo edges stay smooth
f5c2d0f6 chore: merge main into main-menu-tabs
81a9d9df chore: update artwork manager
70ed48ab chore: merge main into main-menu-tabs
a0686e09 feat(nextui): vertical stacks, whole-content tab-focus dim, whole-pixel List rows, long Next split and 65% Backdrop dim
c6cdfb99 refactor(ui): share the hint bar icon placement so the List band can measure it
9865ff36 feat(settings): add Horizontal and Vertical orientation rows for the Carousel and Backdrop layouts
b8685efc feat(nextui): frameless Carousel, Grid logo selection, fixed List band and Backdrop fades on the main menu
9cc52782 feat(settings): default main menu tabs to Carousel and game lists to Grid, Backdrop only for game lists
116aeae5 feat(ui): add one accent colour for selection marks, following the theme's Color 1 and Color 5
a6fda602 fix(tools): shorten long page titles, show locked achievements by state and drop the unused folder names row
44ced9c0 fix(nextui): key main menu caches on a per-list serial and fix return paths, box art fetch and failed deletes
998dc238 fix(ui): key the title fit on the full title, guard the NEON fade on aarch64 and fix the menu animation test
cddfd5d9 feat(tools): use the shared page titles and list layout in the tool apps
eb277cf5 feat(ra): add the achievement detail page, round rich rows and parent page titles
216856d6 feat(minarch): record the original ROM path for achievements and title the in-game menu pages
afb1ab8a feat(settings): add the Layouts page for main menu tab styles and tabs
cdf4943f feat(nextui): tabbed main menu with Home and List, Grid, Carousel and Backdrop layouts
e527b7a9 feat(ui): add shared fonts, fades, page titles and list layout used by every app
```

## Method and confidence labels

- Two Opus agents, read-only, each read whole files (not just diffs) and traced call paths from
  `nextui.c`'s main loop: one over the render/frame path, one over the data/input/startup path.
- **CONFIRMED** = the call path was traced in source by the reviewer and the orchestrator spot-checked
  the cited lines (`sed -n`) afterwards. **PLAUSIBLE** = inferred, not traced end to end, or the cost
  estimate is a guess. Nothing was profiled on hardware; every ms figure is an estimate for an A53.
- Spot-checks done by the orchestrator (all held): `menutabs.c:131/198/215` (`buildRoot` →
  `Directory_new` on every `MenuTabs_step`), `home.c:520/524` (`cardCacheClear()` at the end of
  `rebuild()`, `ensureBuilt` keyed on `MenuTabs_generation()`), `collcount.c:221` (`stat()` in
  `CollCount_get`), `collcount.c:158-167` (per-count snapshot write in the worker loop),
  `nextui.c:410-422` (`*_animating()` → `dirty`), `nextui.c:245` (`Home_reset()` at init).

---

## Tier 1 — user-visible hitches (fix first)

### T1-1. Every tab switch rebuilds the tab's root list from the SD card, on the input frame — CONFIRMED (both reviewers)

**Path:** `gamelist.c:1364` `switchTab` → `menutabs.c:215` `MenuTabs_step` → `openRootKeepFocus`
(`menutabs.c:198`) → `buildRoot` (`menutabs.c:131`) → `Directory_new` (`content.c:358`).

**Triggers:** every L1/R1 press; every LEFT/RIGHT on the tab row; every edge move past the end in
Grid, Carousel, Home.

**What each tab costs:**
- **Consoles:** `getRoms` → `readRomsCache` → `exists()` + `cacheSourcesFingerprint()`
  (`content.c:572`): ~5 stats, `opendir(res/arcade)` + a stat per table, `opendir(Roms)` + 2 stats per
  console folder (folder + `map.txt`). Stock skeleton ships 48 Roms folders → ~105 stats + 2 readdirs.
  Then parse `emulist_cache.txt`; `Directory_index` opens `/mnt/SDCARD/map.txt`.
- **Tools:** 3 readdirs + a shadow `exists()` per pak.
- **Collections:** 2 readdirs (`recoverCollectionParts`, then the listing) + `exists()` per entry.
- **Home:** `getPinned` → `Shortcuts_validate` (one `exists` per pin) + `opendir` per non-pak pin,
  then Home `rebuild()` (see T1-2): builds Entries for all 24 recents just to pick the first ROM,
  `readyResume` (2 stats), `exists` per tool pin.

**Cost (estimate):** 3–15 ms with a warm dentry cache; 50–200+ ms cold on exFAT. It lands in the frame
that should show the tab change, so tapping through tabs quickly hitches. The author's own
`underlineRebase` comment (`menutabs.c:509`) is a workaround for exactly this slow first frame.

**Fix (preferred):** keep one cached root `Directory*` per tab. This replaces `TabMemory`, which today
only stores `selected/start/end` (`remembered[]`). `MenuTabs_step` then just swaps `stack->items[0]`
and still bumps `generation` so per-view state resets as now. Drop a cached root only in:
`MenuTabs_reload`, `Content_invalidateEmulist`, `Shortcuts_add/remove/replacePath`, collection edits,
Artwork-Manager return, rescan.
**Caveat:** a cached root keeps its `serial` across tab switches. Views also key on
`MenuTabs_generation()` so switching is still covered, but **any in-place edit to a cached root's
entries must bump the serial or the generation**, otherwise consumers keyed on serial (pill,
`selectedIsGame`, grid/row `seen_*`, Home `built_*`) go stale.
**Cheaper fallback:** keep rebuilding but memoise `cacheSourcesFingerprint` for the process with a
static `validated` flag that `Content_invalidateEmulist` clears. In-app changes already go through
invalidation; only out-of-band SD edits would be missed until next nextui start (nextui restarts after
every game anyway).

### T1-2. Home drops its whole card cache and recomposes every visible card on every visit — CONFIRMED

**Path:** `home.c:524` `ensureBuilt` rebuilds when `need_rebuild || built_gen != MenuTabs_generation()
|| built_root != stack[0]->serial || built_w/h/scale changed`. Generation and serial both change on
every tab step (T1-1). `rebuild()` re-reads Continue (`Recents_getEntries` + `readyResume`, which stats
the SD), `exists()` per tool pin, and ends with `cardCacheClear()` (`home.c:520`). Every visible card is
then composed in that same first frame: `GFX_renderText` fresh (`home.c:330`, deliberately uncached) +
rounded corners + stats faces.

**Cost (estimate):** ~10–30 ms of text rasterising in one frame, every time you land on Home.

**Fix:** remove `cardCacheClear()` from `rebuild()`. The author's own comment on that line says the
stamps (`cardStamp`) already catch content changes and the clear only frees memory — clear on
`Home_quit` (or memory pressure) instead. Also key the rebuild on Home's own inputs: `Home_reset` is
already called on pin/unpin/rename, so key on that + screen size + `FIXED_SCALE`, not the global tab
generation/serial.

### T1-3. Prefetch costs a full render + `GFX_flip` per item, and never runs while the D-pad is held — CONFIRMED

**Where:** `rowview.c:1788`, `stackview.c:329`, `gridview.c:578-641` compose one item per frame.
`RowView_animating` / `GridView_animating` / StackView return `prefetch_pending`, and `nextui.c:418`
turns that into `dirty = true`.

**What it does:** each of the 6–10 prefetch steps after a settled move is a full render + `GFX_flip`.
In Backdrop that is a full-screen picture copy + bar fade + hint bar + texture upload per step.

**Held D-pad:** the slide tween (260/300 ms) is longer than the key-repeat interval, so
`slide_tw.active` never clears while holding; prefetch never runs, and each repeat step composes the
new centre tile (plain and lit) plus the new item at d=±4 synchronously on the input frame.

**Fix:** move prefetch out of the render pass into the idle branch of the main loop (`nextui.c:668`
region), with a time budget (e.g. ≤ 4 ms per idle tick) instead of "one item per frame", with no
redraw and no flip. Optionally let prefetch run during a slide once the tween passes ~60 % so held
scrolling hits cached tiles. Splitting prefetch out also shrinks `GridView_render` and
`RowView_renderPicture` (see refactors).

### T1-4. Startup does ~2× main's scan work and reads the whole ROM index before the first frame — CONFIRMED

This runs on **every return from a game** launched from Consoles (nextui restarts after each game and
reopens Consoles in its default Carousel). In order:
1. `MenuTabs_init` → `Content_hasConsoles` (`content.c:1286`) runs fingerprint #1, parses the emulist
   and allocates/frees ~48 Entries, only to test `count > 0`.
2. `buildRoot(Consoles)` runs fingerprint #2 and parses the emulist again.
3. The first render's count label (`rowview.c:390` / `gridview.c:204` / `gamelist.c:2002`) calls
   `Content_consoleGameCount` → `buildConsoleCounts` (`content.c:803`), which reads all of
   `romindex_cache.txt` on the UI thread (~350 KB for 3 000 ROMs). The "last hit" lookup in
   `consoleCountCb` mostly misses because rows are sorted by label, not folder, so it degrades to
   rows × folders comparisons. Estimate 10–40 ms.
4. The Collections tab reaches the same read through `CollCount_get` → `Content_libraryFingerprint`.

`main` did one fingerprint and never read the ROM index at boot.

**Fix:** have `getRoms` validate once per process and let `hasConsoles` reuse the result (or add
`Content_consoleCount()` that doesn't build Entries). Write each console's game count as a third column
in the emulist cache — `getRoms` already builds the index, so counts come for free with the emulist
read. Use the emulist's `#fp=` header as the library fingerprint.

---

## Tier 2 — per-frame waste and SD contention

### T2-5. `CollCount_get` does a `stat()` on the UI thread on every call — CONFIRMED path, cost PLAUSIBLE
`collcount.c:221`. Callers: `rowview.c:385` (`countLabel` via `drawSideCount`/`drawCount`) per visible
item per frame; `gridview.c:211/220` (`tileCount`, `plainCount`) per visible collection tile per frame
plus 1–2 for the selection (`gridview.c:566-669`) on every frame of the slide/lit tweens; the List info
band once per frame. On the Collections tab during a slide: 7–14 stats per frame at 60 fps
(~0.2–0.5 ms/frame warm). **Fix:** cache `(mtime, size)` per path for the current generation; refresh on
`CollCount_invalidate`, tab open, or when the worker reports. `Content_libraryFingerprint` is already
memoised.

### T2-6. The CollCount worker rewrites its cache file with fsync after every single count — CONFIRMED
`collcount.c:158-167` → `writeFileAtomic` → fsync (`utils.c:563`), once per computed collection. First
visit to Collections with N collections = N fsync'd SD writes (~10–50 ms each) blocking the shared SD
queue while art is loading. The comment there explains why it persists eagerly: launches leave nextui
via `_exit`, so `CollCount_quit` never runs. **Fix:** write once when the queue drains and has been idle
~500 ms, **and** flush from `saveLast` / the pre-launch path so the `_exit` case is still covered.

### T2-7. HomeStats worker re-parses every cached RetroAchievements game on every nextui start — path CONFIRMED, cost PLAUSIBLE
`nextui.c:245` `Home_reset()` → `HomeStats_request()` unconditionally, even if Home is never shown;
again on every `MenuTabs_reload` (pin, unpin, rename, delete, Refresh) and after any Home context
action. The worker calls `RAT_unlocksSinceCancellable` → `rat_parse_sets` (rcheevos JSON parse of
`sets.json`, often 100 KB–1 MB) + `rat_server_unlocks` for every directory in `.ra/cache/games`. Off
the UI thread, but it competes for the SD card and CPU with the art loaders right after boot while the
CPU policy drops to the menu/idle cap, and costs battery. **Fix:** request lazily on the first Home
render; persist the result keyed on (journal/confirmed files' mtime+size, games-dir mtime, window day);
don't re-request on pin changes (stats don't depend on pins).

### T2-8. `readyResume` runs on any dirty frame, not only on selection change — CONFIRMED (pre-existing pattern, more triggers now)
`gamelist.c:1816` `if (*dirty && total > 0) readyResume(entry)`. `dirty` is set before input handling
by each async completion (thumbnail, GameInfo, CollCount, HomeArt, HomeStats, status bar), each
re-statting the resume slot + `.m3u` (1–3 stats). Also `gamelist.c:1618, 1631`: Grid/Row selection
steps call it on every step. **Fix:** key on `(top->serial, top->selected)`; optionally wait until the
selection settles.

### T2-9. Per-item-per-frame text measuring — CONFIRMED path
- Logo-less consoles: `drawSideCount` / `drawItemCount` (`rowview.c:1400, 1452`) call
  `logoName(NULL, …)` → `Tiles_textBlock` → `GFX_wrapText` → `TTF_SizeUTF8` per item per frame.
  Cache the measured height next to `side_offs`.
- `MenuTabs_renderRow` calls `TTF_SizeUTF8` for 4 labels on every dirty frame (`menutabs.c:398`);
  cache alongside the label strip.
- `Content_consoleGameCount` does ~48 `snprintf` + `prefixMatch` per call, per tile, per frame.
  Precompute the count once per row when the list is built (ties into T1-4's emulist column).

### T2-10. Tile composition pollutes the shared text LRU; font cache may thrash — PLAUSIBLE
- `tiles.c:308` `blitTextColor` composes cached tiles through `GFX_getCachedText`, a shared
  48-entry LRU (`api.c:338`). Each composed tile pushes 1–4 one-off lines into it, evicting the
  per-frame users (page title, tab labels, hint bar, List rows, InfoBand), which re-rasterise on the
  next frames — worst during held scrolling. **Fix:** text baked into a cached surface should use
  `GFX_renderText` + `SDL_FreeSurface` directly, as `home.c:330` already does; keep
  `GFX_getCachedText` for text redrawn every frame.
- `ui_font.c:73` runs `TTF_OpenFont(font1.ttf)` on each miss of a 32-entry LRU.
  `skeleton/SYSTEM/res/font1.ttf` is 8 MB (CJK); opening it costs ms (cmap/loca parse). Evicting a font
  also calls `GFX_forgetFontText`, wiping that font's text-cache entries. Compose paths ask for many
  whole-sp sizes (`collLayout` and `drawNameTile` shrink loops, `Row_collFitSlotSp`,
  `carouselToolFont`, Grid/Carousel sizes scaled by `tile_w/140`, Home 11–24 sp). If the working set
  passes 32 sizes (larger UI scale, Vertical stacks) it thrashes on compose frames. **Fix:** add a
  debug counter of opens/minute to confirm; then open one face and use `TTF_SetFontSize`
  (SDL_ttf ≥ 2.0.18; `ui_font.c:148` already version-checks), or quantise sizes and raise the cap.
  This also removes the fragile "a font pointer is only valid until the next `UIFont_get`" rule.

### T2-11. Edit actions re-run all of the tab setup — CONFIRMED, one-off cost
`MenuTabs_reload` (`menutabs.c:225`) calls the full `MenuTabs_init` for every pin, unpin, rename and
delete: `Recents_load` (24 recents × `hasEmu`, up to 3 `exists` each ≈ 72 stats), `Shortcuts_validate`,
a fingerprint, the `hasCollections` readdir. Delete ROM is heaviest: 3–4 fingerprints
(`Content_romCachesFresh`, `Content_forgetRom`, init, `buildRoot`) + 2 fsync'd cache rewrites,
20–100 ms. Acceptable as is; cheap to trim with a "what changed" mask that skips rebuilding the root
when the change can't affect the current tab (e.g. a pin while on Consoles).

### T2-12. HomeArt single worker: slow art, not input lag — PLAUSIBLE
`homeart.c:118` `cropFill` is a scalar bilinear with a per-pixel, per-channel `lerpPixel`;
`buildBackdrop` (1280×720 + gain pass) and `addShadow` are similar full passes; `placeholderFor`
encodes a PNG to the SD card on the same worker. None of it is on the UI thread, but with one worker a
Backdrop decode (~50–150 ms) or a PNG save delays the box art for the newly centred item, and
in-flight work for an item scrolled past can't be cancelled. **Fixes:** reuse `AreaScale_argb` or add
a NEON bilinear; write placeholder PNGs from a low-priority second pass; re-check `slot->gen` between
decode and shaping stages and bail early; the worker is not pinned (`PWR_pinHelperThread` is never
called in `workerMain`).

### T2-13. Backdrop frame cost — PLAUSIBLE, low priority
Every slide/crossfade frame copies or lerps the full screen (`rowview.c:1650-1677`, `UI_blitOpaque` at
3–3.7 MB); each selection change also runs `settlePicture` (`rowview.c:1574`), another full-screen pass
on the input frame; `GFX_flip` then uploads the full screen again. ~6–12 ms/frame on an A53 — fits the
budget, little headroom on the Brick. Only if profiling shows it: restore only the row band from the
picture during a slide with no crossfade.

### T2-14. Micro
- `itemFind` (`rowview.c:524`): linear `strcmp` over 32 keys ~300 bytes long after an `snprintf` of
  the full path, 2–3 lookups per visible item per frame. Microseconds; a 32-bit hash next to the key
  makes it free.
- The Carousel side tile is a nearest-pixel `SDL_BlitScaled` of the full-size screenshot
  (`tiles.c:223`) while slot rows use area-averaged `sideCopy` — quality inconsistency, not a cost.
- Each Entry costs 3 allocations (path, name, unique); `Entry_newNamed` computes a display name it
  immediately throws away. Not a problem at these list sizes.

---

## Correctness bugs found by the perf pass

### C-1. Wrong art in Carousel tiles after "Fetch artwork" — CONFIRMED
The `carouselTile` key (`rowview.c:580`) is `C|w|h|kind|lit|colours|state|path|name`.
`HomeArt_forget` (`gamelist.c:1088`) makes HomeArt re-decode, but the READY key (state = 2) is
byte-identical to the old one, so `itemFind` returns the previously composed tile (placeholder or old
art) until the 32-slot LRU evicts it. Grid avoids it (`tileStamp` hashes `t->picture`) and Backdrop's
`sideArt` keys on `%p` — both carry a pointer-reuse (ABA) risk. **Fix:** expose a per-slot generation
from HomeArt (`slot->gen`) and put it in the key/stamp in all three places instead of raw pointers; or
add `RowView_forget(path)` that drops matching items.

### C-2. `/code-review 139 high` correctness pass — 10 findings (reported 2026-10-02 23:56)

Ranked by the reviewer, most severe first. Items marked ⟷ overlap a perf finding above.

- **C-2a. `MenuTabs_reload` leaves pushed Directories over another tab's root — CONFIRMED by orchestrator
  (`menutabs.c:224-246`).** When the current tab vanishes, `next` resolves to a different tab and
  `stack->items[0]` becomes that root, but `stack[1..]` and `top` stay (only `stack->count == 1`
  re-points `top`). Scenario: inside Consoles › GBA (the only console with ROMs), Delete the last ROM →
  `Content_forgetRom` sees `!hasRoms` → `Content_invalidateEmulist` → `MenuTabs_reload` →
  `MenuTabs_init` drops the Consoles tab → stack[0] = Tools/Collections root while stack[1] (the empty
  GBA list) is still `top` titled "Consoles | Game Boy Advance"; B lands on the wrong tab, and
  `generation++` with an unchanged `top` makes Grid/Row caches re-sync against a list that never
  belonged to that tab. **Fix:** when `next != current`, free everything above stack[0] and set
  `top = fresh` (a tab change can't keep a foreign sub-list), or pop to root before resolving.
- **C-2b. Game Tracker round thumbnails break on alpha-less art (`gametime/gametime.c:196`).**
  `loadRomImage` keeps the source pixel format; new `maskCircle` no-ops when `BytesPerPixel != 4`, and
  `SDL_MapRGBA` drops alpha on a 32 bpp surface with `Amask == 0`. A 24-bit PNG / JPEG `.media`
  screenshot → square thumbnail among round ones (the retired `GFX_ApplyRoundedCorners` at least
  blanked the corners). **Fix:** `SDL_ConvertSurfaceFormat(…, SDL_PIXELFORMAT_ARGB8888)` before
  masking.
- **C-2c. `GFX_quit` font/asset leaks (`common/api.c:789`).** Closes `hint_tiny_native` but never
  `hint_tiny_native_ar`, nor `hw_native`'s three fonts and its asset sheet; `GFX_loadSystemFont` also
  opens `hint_tiny_native(_ar)` on every (re)load even when `NATIVE_SCALE == FIXED_SCALE` where
  `hintTinyNative()` never uses it. Every app that calls `GFX_quit` (Settings, tools) leaks 2 TTF_Font
  handles (+3 fonts and a full `assets@2x.png` surface at non-native UI scale).
- **C-2d ⟷ T1-4/T2-11.** `MenuTabs_reload` → `MenuTabs_init` → `Content_hasConsoles()` builds the whole
  console list via `getRoms()` only to test `count > 0`, then `buildRoot(Consoles)` calls `getRoms()`
  again, plus `Recents_load()` and `Shortcuts_validate()` stat the card on every pin/unpin/rename/delete
  (`menutabs.c:227`). Derive `hasConsoles` from the entries `buildRoot` already builds or the cached
  emulist row count.
- **C-2e ⟷ T2-7.** `MenuTabs_reload` unconditionally `Home_reset()` → `HomeStats_request()` → full RA
  `sets.json` parse of every cached game on every pin/unpin/rename/delete/Refresh (`menutabs.c:246`).
  Only `need_rebuild` (Continue + pins) was needed; the stats face did not change.
- **C-2f ⟷ T2-5.** `CollCount_get` `stat()`s per call; `GridView_render` calls it per visible
  Collections tile per frame including each frame of the 260 ms slide / 120 ms crossfade
  (`collcount.c:221`): ~10 stats × 16+ frames per move. Cache per Directory serial as
  `Content_consoleGameCount` already does.
- **C-2g. Duplicate "push Tools over the current tab" (`launcher.c:646` `restoreToolsOverTab` vs
  `gamelist.c:1174` `GameList_runContextAction` case 2).** Same `Directory_new(TOOLS_PATH)` + windowing
  + `Array_push` + `top =`, already diverging (context-menu copy calls `MenuTabs_leaveFocus` and does
  not clamp the window to the selection; the boot copy does the reverse). One
  `pushToolsOverTab(const char* select_path)` in launcher.c.
- **C-2h ⟷ refactor 3.** `gridview.c:51` and `home.c` each redefine `Tween` +
  `tweenProgress/Start/Tick/animationsOn` that `rowview_shared.h` already declares; gridview also
  duplicates rowview's `kindFor/resetKinds/displayName` and the `selectTile` List-window clamp that
  `RowView_handleInput` and stackview's `selectItem` carry (4 copies of the kind classifier, 3 of the
  clamp).
- **C-2i. `GFX_blitHardwareGroup` swaps process-wide globals (`common/api.c:2232`).** Draws the status
  group at native size by temporarily overwriting `font.small/tiny/micro`, `gfx.assets`, `asset_rects`
  and `ui_scale` around `hardwareGroupDraw`; the tab row (`TAB_DP`) and hint bar (`NATIVE_SCALE * …`)
  hand-roll a third and fourth "native size" mechanism on top of `SCALE1`. Risks: `GFX_getCachedText`
  keys on the `hw_native` font pointers that `hwNativeFree` later closes without `GFX_forgetFontText`;
  any future early return inside `hardwareGroupDraw` leaves globals swapped for the frame. **Fix:** a
  draw-context struct (scale, fonts, sheet, rects) passed to `hardwareGroupDraw`, plus one shared
  `NX_NATIVE_DP` helper for menutabs.c and ui_buttonhintbar.c.
- **C-2j. Third copy of the console collation rule (`content.c:833`).** `Content_consoleGameCount`
  re-inlines "path prefix up to and including the last `(`" that `getRoms` and `getEntries` already
  inline (same `strrchr` / `paren[1]='\0'` / `prefixMatch`). All three share the quirk that
  `Roms/Ports` collates `Roms/Ports2`. A `collatedConsolePrefix(path, out)` helper keeps count and
  listing in step.

---

## Wave 2 plan — T1-1 + T1-4 + C-2a + C-1 as one change set (planned 2026-10-03 00:10)

Produced by a read-only Plan agent against the tree after wave 1. Line numbers are from that tree;
look up spots by function name.

### Key design decision: who owns `stack[0]`
The stack keeps owning `stack->items[0]`. Each tab's slot holds only the roots of tabs that are **not
current**, so `slots[current].root == NULL` always and no pointer is shared between the stack and a slot.
- Switching tabs: move `stack[0]` into `slots[current]`, move `slots[next]` into `stack[0]`.
- Why: every existing free of stack[0] stays correct with no edits — `reloadDirectoryAt(0)`
  (gamelist.c:209-237), `openDirectory` → `DirectoryArray_free` (launcher.c:568), the PLATFORM merge in
  `pathToStack` (launcher.c:480-490), `Menu_quit` (nextui.c:89). Pointing a slot at stack[0] would
  double-free in all of these.
- The serial caveat goes away: parked roots are unreachable, and no code edits any `entries` array in
  place (entries are fixed once `Directory_new` builds them). The only in-place writes are
  `selected/start/end` on the current root (`doRenameCollection` gamelist.c:874-885, `loadLast`
  launcher.c:697-712 and 759-776, input handling), which consumers already compare.

### Steps (tree compiles after each)
1. **`nextui/emulist_model.c/.h` (new, no SDL) + `tests/test_emulist_model.c`.** Add the .c to the
   nextui Makefile `SOURCE` (line 28) and a cc/run pair to `run_tests.sh`.
   `Emulist_collatedPrefix(path, out, n)` (cut after the last `(`, C-2j); `Emulist_parseRow(line, &path,
   &name, &count)` (exactly `path\tname\tcount`, decimal count ≥ 0, nothing after); `Emulist_formatRow`;
   `Emulist_collateCounts(console_paths, n, folder_paths, folder_counts, m, out)` (sum of folder counts
   whose path starts with the console's collated prefix, case-insensitive like `prefixMatch`);
   `Emulist_adjustCount(row_path, removed_folder_path, dropped, *count)` clamped at 0.
   Tests: 2-column row rejected; `"12x"`, `"-1"`, empty count rejected; a name containing `(`; GB/SGB
   siblings combine under `Game Boy (`; the Ports/Ports2 quirk is kept (Ports counts Ports2, as the
   listing does); a folder with no paren; adjust clamps at 0.
2. **Route the three collation copies through the helper (C-2j):** content.c:1260-1265 (`getRoms`),
   the `getEntries` copy, content.c:830-835 (`Content_consoleGameCount`). No behaviour change.
3. **Validate the emulist once per process (T1-4a).** content.c memo: `Array* emu_memo` (raw entries),
   `int* emu_counts`, `uint64_t emu_fp`, `bool emu_valid`, `unsigned lib_gen`. `ensureEmulist()` runs
   today's `readRomsCache()` once, scans on a miss, fills the memo, bumps `lib_gen`. `getRoms()` returns
   a deep copy (`Entry_newNamed(path, type, name)` per entry — copy only path/type/name;
   `Directory_index` sets `unique`/`alpha`). Clear the memo in `Content_invalidateEmulist` (868) and every
   branch of `Content_forgetRom` (936-970). **Gotcha `Content_searchRoms` (982-995):** when
   `readRomIndexCache()` is stale, drop the memo before `getRoms()`, else the index is never rebuilt and
   Search shows old labels after a ROM rename (rename edits the console's map.txt, part of the
   fingerprint). Add `int Content_consoleCount(void)` and `unsigned Content_libraryGen(void)` to
   content.h; `Content_hasConsoles` (1286) becomes `Content_consoleCount() > 0`. Verify: log
   `cacheSourcesFingerprint` calls — a boot into Consoles shows exactly one.
4. **Per-console counts as a third column (T1-4b/c).** In `getRoms`, record the change in
   `rom_index->count` around each `indexRomDir` call keyed by folder; after the loop
   `Emulist_collateCounts` (matches today's `consoleCountCb` totals, including siblings indexed twice).
   New `writeEmulistCache` (`writeEntryCache` stays for the romindex). Reader: `forEachEntryCacheLine`
   splits at the first tab only, so the "name" arrives as `name\tcount` — split at its last tab; any row
   failing `Emulist_parseRow` → return −1 → stale. Bump `CACHE_SCHEMA_TAG` to `"romindex-v6"` (557).
   **`Content_forgetRom` (963-968):** `rewriteEntryCache` gets an out-param for dropped rows; the emulist
   rewrite lowers the count via `Emulist_adjustCount` (otherwise `rewriteCacheCb` copies `name\tcount`
   through unchanged and the count silently drifts). `Content_consoleGameCount` looks up the exact path
   in the memo, falls back to the memo row with the same collated prefix (a pinned sibling such as
   `(MD)`), else 0 while the memo is valid. `Content_libraryFingerprint` returns hex of `emu_fp`, or
   `"empty"` with 0 rows. Delete `ConsoleCount`, `consoleCountCb`, `buildConsoleCounts`,
   `dropConsoleCounts` (741-817). Other romindex readers are unaffected: Search has its own fingerprint
   check; CollCount only needs the fingerprint string and its worker uses
   `Content_forEachCollectionGame`; settings.c:1606, sync.c:1157 and the Xtras scripts only `rm` the files.
5. **`menutabs_model` additions + tests.** `MenuTabs_clampWindow(count, rows, *sel, *start, *end)`:
   today's `buildRoot` reset rule (131-144) plus re-windowing when `end-start != min(count, rows)`
   keeping `sel` visible (cases: empty list, list shrank, rows grew/shrank, sel on the last row).
   `MenuTabSlot {void* root; bool hint; int selected, start, end;}` with `MenuTabs_slotPark(slots, id,
   root, sel, start, end)` (returns the displaced root to free), `MenuTabs_slotTake(slots, id)`,
   `MenuTabs_slotDrop(slots, id)` (returns the root to free, keeps the selection as a hint). Tests with
   fake pointers: park/take round-trip; take on empty → NULL; park into a full slot returns the old root
   (no leak); drop keeps the hint; never the wrong tab's root.
6. **C-2a in `MenuTabs_reload` (menutabs.c:225-247).** `next != current`: pop `stack[1..]` with
   `DirectoryArray_pop` until count is 1, free the old root, build `next` from its hint, `top = fresh`,
   `MenuTabs_leaveFocus()`. `next == current`: rebuild stack[0], keep the sub-stack as today. Callers'
   `if (stack->count > 1) reloadDirectoryAt(...)` (gamelist pin/unpin/delete) then skip correctly —
   today in the "delete the last GBA ROM" case it rebuilds the empty GBA list on top of the wrong tab.
   Verify by hand: Consoles › GBA (only console), delete the last ROM → land on the next tab's root, B
   does nothing.
7. **Cached roots (T1-1).** menutabs.c: replace `TabMemory remembered[]` (34-39) and `rememberCurrent`
   (147-152) with `static MenuTabSlot slots[MENU_TAB_COUNT]` + `unsigned lib_gen` for Consoles.
   `openRootKeepFocus` (198-208): pop everything above stack[0]; park stack[0] only if
   `exactMatch(path, MenuTabs_path(current))` else free it; `current = id` before anything reads the row
   count; take the slot (a Consoles root is dropped if its `lib_gen != Content_libraryGen()`); on a hit
   re-clamp with `GameList_rowCountAt(true)` (rows depend on layout/scale); on a miss `buildRoot` with
   the hint; keep `generation++`. `MenuTabs_reload`: drop every slot (selections stay as hints) — the
   global invalidation. `MenuTabs_setCurrent` (84): drop `slots[id]` because `pathToStack`
   (launcher.c:439) just built a fresh root. New `MenuTabs_dropCached(id)` from `doAddToCollection`'s
   new-collection branch (gamelist.c:650-660, no reload today). New `MenuTabs_quit()` from `Menu_quit`
   (nextui.c:86). Memory: ≤ 3 parked roots, a few hundred Entries, < 100 KB. Home `pins[]` borrow from
   the Home root (home.c:104): valid while parked; a dropped root gets a new serial so home.c rebuilds.
8. **C-1, HomeArt generation.** `Slot.gen` already exists (homeart.c:53), fresh on create (497) and
   forget (553). Expose `unsigned HomeArt_lastGen(void)`: the gen of the slot the most recent `lookup`
   returned (UI thread only, read right after the call; a static set in `lookup` 460-514). Use it in
   FOUR places: rowview `carouselTile` key (add `|%u` next to `state`); rowview `sideArt` key (replace
   `%p` with the gen of the producing `HomeArt_boxart` call); gridview `TileSpec.picture_gen`
   (tiles.h:28), set beside `t->picture` (gridview.c:469), hashed in `tileStamp` (275); home.c
   `artStamp(as, pic)` in `cardStamp` — also pointer-keyed, missed by the review. Verify by hand: Fetch
   artwork on a Carousel game, close the modal, new art shows at once.

### Invalidation table ("global" = `MenuTabs_reload` drops every slot)
| Mutation | Where | Slots dropped | Serial / generation |
|---|---|---|---|
| Refresh ROMs | gamelist.c:1165 invalidate, 1166 reload | global; Consoles also via `lib_gen` | new Directories/serials; generation++ |
| Pin (`Shortcuts_add`) | shortcuts.c:148; gamelist.c:1185-1186 | global (only Home changes) | same |
| Unpin (`Shortcuts_remove`) | shortcuts.c:167; gamelist.c:1195-1196 | global | same |
| Delete ROM (`Content_forgetRom`) | gamelist.c:1249, reload 1253; content.c:936-970 | global; memo cleared → `lib_gen` | same; counts from the memo |
| Rename ROM (alias + recents) | gamelist.c:742-759, reload 1265 | global (Home pins, Continue) | same |
| Rename collection (+`replacePath`) | gamelist.c:854, 867, reload 872 | global | same; re-window 874-885 touches selection only |
| Delete collection | gamelist.c:~898, 908, reload 910 | global | same |
| New collection via Add to Collection | gamelist.c:650-660 (**no reload**) | Collections (`MenuTabs_dropCached`) | rebuilt next visit, new serial |
| `pathToStack` → `MenuTabs_setCurrent` | launcher.c:439 | that tab | stack[0] is fresh |
| `Content_invalidateEmulist` without reload | content.c:868 | Consoles via `lib_gen` | — |
| Fetch artwork | gamelist.c:1088 | none (art, not entries) | HomeArt gen (step 8) |
| Remove recent (Game Switcher) | gameswitcher.c:147 | none (`Home_reset` runs) | — |
| Launch / pak / script return, Artwork Manager, Xtras, Settings refresh, sync | launcher.c:21-25 `queueNext` exits the process | none: the cache dies with the process | — |
| Favourite toggle | does not exist | — | — |

### Emulist cache format
- Before: `#fp=<16 hex>` then `path\tname`. After: `#fp=<16 hex, seeded "romindex-v6">` then
  `path\tname\tcount`. The romindex keeps its layout (its fingerprint changes via the tag bump).
- Stale = fingerprint differs, romindex missing, or any row not 3 columns with a valid count. Stale →
  full rescan, never a crash. Upgrade cost: one rescan + one CollCount recount per collection (the
  library fingerprint string changes once).

### Risks
- Copy only `path/type/name` out of the memo. Home stops rebuilding on revisit once T1-1 lands
  (`built_root` compares serial only) — anything Home shows that isn't tied to stack[0] or `Home_reset`
  needs its own trigger. Search must not trust the memo (step 3). Order: 1-4 content.c + model; 5-7
  menutabs/launcher; 8 last.

### Corrections to the review above (from the planner)
1. T1-1's drop list names "Artwork-Manager return, rescan": pak launches restart nextui (`queueNext`),
   so those returns start with an empty cache. It missed two real cases: new-collection creation
   (gamelist.c:650, no reload) and `MenuTabs_setCurrent` from `pathToStack`.
2. `Shortcuts_add/remove/replacePath` need no hooks: every call site is already followed by
   `MenuTabs_reload`.
3. The "in-place edit must bump serial" caveat has nothing to apply to (see design decision).
4. C-1: `slot->gen` already exists; Home's `cardStamp`/`artStamp` is a fourth pointer-keyed cache.
5. T1-4(b): `Content_forgetRom` rewrites the emulist by copying each row after its first tab — a count
   column would drift without `Emulist_adjustCount`.
6. The T1-1 "cheaper fallback" (memoise the fingerprint) would leave Search showing old labels after an
   in-app rename until restart — don't take it.
7. T1-4 step 4 (CollCount) is not an extra read: `buildConsoleCounts` is shared, one romindex read per
   process in total.
8. C-2a: the caller's `reloadDirectoryAt(stack->count-1)` also rebuilds the empty GBA list on top of
   the wrong tab.

---

## Refactor wave plan (planned 2026-10-03 03:14, zero behaviour change, base 44d9f5f1 + the Ports art fix)

**Verdicts.** DO: C-2h helpers (tween/units/displayName/selectedIndex/fnv/kinds/longestWord/collection-name shrink/
window clamp); C-2i shared native-size macros; T2-11 reload mask; ListWindow model (biggest test gap);
`GameList_runContextAction` split; `Recents_firstRom`; InfoBand text-cache fix (it does use the shared LRU).
DEFER: tile kind in `Directory_index` (would stat folder games up front); `fitFont`/`carouselToolFont` merge (different
floors → font sizes would change); C-2i draw-context struct (both risks already closed by C-2c and the wrap-and-restore
structure; real version needs context variants of ~8 helpers + notification.c); `ListIdentity` (8 sites key on
different field sets); generic worker (CollCount/HomeStats/HomeArt have diverged); render-function splits (untested
visual churn); shared LRU (C-1 already fixed by `HomeArt_lastGen`).

**Waves.** W1 parallel: C1, A, E1, F, G, D1. W2 parallel: C2a, C2b (need C1; C2a needs A). W3: D2 (needs C2b, D1,
E1). W4: H (needs D2).

- **C1 ListWindow model** — new `nextui/list_window.c/.h`, `tests/test_list_window.c`, run_tests.sh, Makefile.
  `fromTop/toBottom/selectAtTop/reveal/reload/step(may_wrap)/page`; each body a verbatim copy of the inline block it
  replaces; test carries pasted `legacy_*` copies and checks every (total 0..12, rows 1..8, sel, window) + named cases
  (wrap both ways, stop-at-edge while held, page at ends/mid, letter jump, reload clamp keep>n / n<rows / n=0).
- **A C-2h helpers** — new `view_common.h` (Tween, animationsOn, tweenProgress/Start/Tick, pxPerDp/pxPerSp/barPx/
  BAR_DP, View_displayName, View_selectedIndex, View_fnv/fnvStr with NULL→"\xff" Grid rule); rowview_shared.h includes
  it; byte-identical copies removed from gridview.c (48, 51-54, 78-102, 121-127, 141-146, 188-192, 271-280), home.c
  (118-121, 138-171, 413-417), rowview.c (59, 177-197, 207-209, 316-321, 368-372), stackview.c (85-90). Reconciled:
  home.c `fnvStr` hashes NULL as "" → keep a 1-line local wrapper (stamps unchanged); content.c `fnv1a64` and
  placeholder_art.c `fnv` untouched (persisted hashes); Grid's kinds cache dropped for `RowView_syncKinds/kindFor`
  (same body, same reset key (serial, generation, n); remove Grid's `free(kinds)`); `Tiles_longestWord` with rowview's
  rules (space+tab split; equivalent for any FAT/exFAT name); `Tiles_collNameSp(name, start, count_sp, avail)` for the
  two shrink loops. No host test (SDL) → both builds + device pixel check.
- **E1 native-size macros** — defines.h `NATIVE1(a)`, `NX_NATIVE_DP(x)` (= TAB_DP's exact expression
  `(int)(x*NATIVE_SCALE*30/42+0.5)`), `NX_NATIVE_SP(x)`; sites ui_buttonhintbar.c 37/43/47/68/71/78, api.c 2261 +
  font opens 459/479/2245-2247, ui_menubar.c 16. Same operand order (do NOT fold into pxPerDp: rounds differently).
  api.c is shared → every app rebuilt; check hint bar/status group/titles at native + non-native scale.
- **F InfoBand** — infoband.c:68 `drawShadowedText` → `GFX_renderText` + free (baked into cached surfaces by tiles,
  Home, rowview; only per-frame caller is the Game Switcher, already uncached).
- **G Recents_firstRom** — recents.c/h; `Home_continueEntry` (homeart.c 647-660) becomes a wrapper; same pick (first
  available non-.pak recent).
- **D1 reload plan model** — menutabs_model: `MENU_RELOAD_{PINS,RECENTS,ROMS,COLLECTIONS,TOOLS,ALL}`,
  `MenuReloadPlan MenuTabs_reloadPlan(unsigned what)` {validate_pins, load_recents, check_consoles,
  check_collections, check_all, stale_tabs}; tests.
- **C2a** — view clamps (gridview `selectTile` 760-768, rowview 1985-1990, stackview `selectItem` 376-381) →
  `selectAtTop`; launcher.c `pathToStack` 443-446/496-497/502-503 + `openDirectory` 562-563 → `fromTop`; `loadLast`
  714-720/778-784 → `selectAtTop` (old code never clamped start at 0; unreachable with same row count).
- **C2b** — gamelist.c `reloadDirectoryAt` 230-243 + menutabs.c `MenuTabs_reload` 276-284 → `reload`;
  `doRenameCollection` 891-899 → `reveal`; `contentToBottom` 1517-1519 → `toBottom`; `GameList_handleInput` up/down
  1743-1768 → `step(may_wrap = PAD_justPressed)`, pages 1771-1797 → `page`, letter jumps 1815-1819/1830-1834 →
  `selectAtTop`.
- **D2 reload mask applied** — `MenuTabs_reload(int keep, unsigned what)`: generation++ always; drop only
  `stale_tabs` slots; `MenuTabs_init` work split behind `static MenuTabInputs last_in`; if next==current and not stale,
  keep stack[0] but re-run `ListWindow_reload` (today's rebuild re-centres); `Home_reset()` always. Masks: Refresh
  ROMs ALL (1181); Pin/Unpin PINS (1193/1203); Delete ROM ALL (1260); Rename ROM PINS|RECENTS (1272); Rename/Delete
  collection PINS|COLLECTIONS (887/925). Invariants: only a masked tab's root can change; visibility inputs outside the
  mask don't change; without RECENTS recent.txt/CHANGE_DISC_PATH untouched; every mask includes PINS so
  `Shortcuts_validate` always runs; a kept parked root == one rebuilt from its hint. Also `TAB_DP` → `NX_NATIVE_DP`
  (menutabs.c:420), `NATIVE_SCALE*…` → `NATIVE1` (666/680/760).
- **H** — `GameList_runContextAction` 1169-1365 → one static per case (`ctxRefresh/Pin/Unpin/DeleteRom/RenameRom/
  AddToCollection/Netplay/EmuOptions/FetchArt`); `break` → `return`; switch + epilogue stay.
- **Already done / no longer applies:** refactor items 5, 8 (consoleCount), C-2g, buildRoot windowing, C-2j,
  prefetch out of render, C-1, item 9; C-2i's two stated risks.

---

## Cache keying notes (per-list serial from 44ced9c0) — verified OK
- The serial is assigned inside `Directory_new` (`content.c:362`), so every rebuild path (reload, tab
  switch, `pathToStack`, push) gets a new one automatically; no path can forget to bump it.
- In-place edits after a reload (`doRenameCollection` re-windowing, `loadLast`) only move the
  selection, which consumers already compare.
- Views also key on `MenuTabs_generation()`. The Grid tile cache keys on a content stamp (name, logo,
  picture, count, accent), so renames and art fetches can't leave a stale tile there.
- **If T1-1 is taken** the serial no longer changes on tab switch; see its caveat.
- No O(n²) list building: dedupe is on sorted neighbours, the sort comparator is a plain
  `strcasecmp` with no allocation, the console-count scan is O(rows × folders). Nothing sorts at render
  time.
- No leaks found on tab switch or navigation: old root and stack freed, Home context entry copy freed
  on close, tile and art caches bounded.

## Already done well — do NOT "fix"
- Main loop sleeps when idle, presents with vsync, wall-clock tweens with a single "tick once more"
  settle; nothing busy-loops. Every keep-alive (`MenuTabs_animating`, dim step, Grid/Row/Home tweens,
  one-tile-per-frame prefetch) settles to false, so idle frames don't redraw.
- `common/ui/ui_fade.c`: rounding (exact ÷255) is correct, 16-bit intermediates can't overflow,
  unaligned `vld1q` loads are fine, the `__aarch64__` guard around `vmaxvq`/`vminvq` is correct with an
  ARMv7 scalar fallback; the screen is ARGB8888 (`common/generic_video.c:941`) so the fast paths are
  always taken. Region-limited dimming (`darkenExcept`, `blitCardOver` corners, caption ink rect) is good.
- Items are composed once per rest size and only nearest-scaled while changing size; Grid and Home
  recompose in place by stamp; page-title fitting is memoised; eased fade surfaces are cached.
- HomeArt: picture pools evict independently, newest-request-first decoding, gen-checked hand-back;
  far-to-near draw order gives the centre item highest priority.
- Background resolve is short-circuited per entry (`gamelist.c:139`); `selectedIsGame` and the
  netplay/options marker probes are cached per entry; Grid/Carousel cache each item's tile type lazily;
  the info band is a cached block; GameInfo, CollCount, HomeStats run on workers with
  latest-request-wins slots.
- D-pad repeat is consistent: rows move on repeat; tab switches and edge moves need a fresh press. The
  Backdrop exit fade ends early on a key press and that press is handled on the next screen. Layer
  uploads are deferred while the selection pill or tab underline glides. `CFG_init` doesn't sync while
  parsing. `MENU_TAB_PATH` and `LAST_PATH` live in `/tmp`.
- `workspace/tg5040/install/boot.sh` and `tg5050/install/boot.sh` diffs only touch the `MinUI.zip`
  update branch (updater swap by rename). No new per-boot check — the "no per-boot checks" rule holds.

---

## Refactor opportunities (ranked)

1. **Windowing math is duplicated ~10×:** `reloadDirectoryAt`, `MenuTabs_reload`, `buildRoot`,
   `doRenameCollection`, `loadLast` (twice), `restoreToolsOverTab`, `pathToStack`, `openDirectory`,
   `contentToBottom`, and the List up/down/page/letter code in `GameList_handleInput`. Extract a pure
   `ListWindow_select` / `ListWindow_step(total, sel, start, end, rows, input)` into a model file and
   host-test it with the existing `common/tests` harness. This is also the biggest test gap.
2. **Three LRU item caches with three keying schemes:** rowview string keys, gridview path + content
   stamp, home kind/pin + stamp. Gridview's (identity + content stamp, recompose in place) is the best;
   making it the shared helper also fixes C-1.
3. **Duplicated helpers across gridview/rowview/home:** `tweenProgress`/`tweenStart`/`tweenTick`,
   `animationsOn`; `pxPerDp`, `barPx`, `BAR_DP`; `kindFor`/`resetKinds`/`syncKinds` (gridview, rowview —
   compute the type once in `Directory_index` instead); `displayName` ×3; `selectedIndex` ×3; `fnv` ×2;
   `longestWord` (rowview, tiles); the "shrink sp until the longest word fits" loop ×4 (`collLayout`,
   `drawNameTile`, `fitFont`, `carouselToolFont`). → `view_common.h` for tween/units/entry-kind cache,
   and a `Fit_wordSp()` in tiles.
4. **"Did the list change?" re-implemented ~8×** with `(serial, generation, count)` statics: the pill,
   `selectedIsGame`, Home `built_*`, grid/row `seen_*`, row `kinds_*`/`pic_top`/`exit_top`, stack
   `seen_*`. One `ListIdentity` helper replaces them.
5. **Per-tab state is scattered:** `TabMemory` + `stack[0]` + globals. A per-tab
   `{Directory* root; bool stale;}` falls out of T1-1.
6. **Single-slot async worker copied 3–4×** (GameInfo, CollCount, HomeStats, HomeArt): same
   init/quit/pending/loaded code. A small generic worker removes the copies.
7. **Oversized functions:** `GameList_handleInput` (`gamelist.c:1556-1868`, ~310 lines),
   `GameList_render` (~315), `GameList_runContextAction` (~200; make each context action its own
   function), `RowView_renderPicture` (mixes picture state with drawing), `GridView_render` (~200;
   mixes layout, info requests, drawing, prefetch, edge shading). `rowview.c` is 2 042 lines.
   Splitting prefetch out (T1-3) shrinks both render functions.
8. `Content_hasConsoles` and `Home_continueEntry` build whole arrays to read one fact →
   `Content_consoleCount()` and `Recents_firstRom()`.
9. Fragile font API — see T2-10.

## Test coverage

**Covered (pure geometry and rules):** `row_model`, `stack_model`, `home_layout`, `infoband_layout`,
`grid_layout`, `list_layout`, `title_fit`, `caption_fit`, `menustyle`, `menu_transition`,
`menutabs_model` (visibility, wrap, resolve, `forPath`, `pickInitial`, `scrollOffset`, key parsing),
`collcount_model` (line parse, format, match), `HomeStats_compute`, `homeart_model`.

**Not covered:**
- List navigation and windowing: wrap, stop-at-edge while held, page jump, letter jump, reload clamp.
- `MenuTabs_reload` selection carry-over.
- `readSavedState` / `MENU_TAB_PATH` second-line round trip (only `parseKey` is tested).
- `Directory_index` label runs.
- `getRoms` cache freshness and fingerprint.
- `Content_consoleGameCount` folder grouping.
- `Content_forgetRom`.
- CollCount worker semantics: latest request wins, −1 is not retried.

---

## Execution ledger

**2026-10-03 03:35 — REFACTOR WAVE IMPLEMENTED, UNCOMMITTED (27 files, +647/−724 incl. the Ports art fix).** All
chunks landed as planned; host suite 36 green (new `test_list_window` = 620,828 legacy-equivalence comparisons,
`test_menutabs_model` +4 reload-plan cases, `test_art_path`). Chunk notes beyond the plan:
- A: Grid now calls `RowView_syncKinds(n)` at the top of `syncList` every frame (the cache is shared, so it must be
  keyed to Grid's list — a no-op when the key matches); shared shrink loop stops on a NULL font (tiles' rule; same as
  rowview whenever avail ≥ 0, always true); `Tiles_longestWord` vs Grid's old 255-byte copy only differ for multibyte
  names > 255 bytes. `view_common.h`: unused `api.h` include dropped (orchestrator).
- E1: `NATIVE1(a)` is `((NATIVE_SCALE) * (a))` to keep the sites' operand order. 16 apps compile api.c.
- C1/C2: only reconciled difference is `loadLast` clamping `start` at 0 where the old code could go negative
  (unreachable with the same row count). Not modelled, left inline: root UP-at-row-0 → tab row, `openDirectory`'s
  restore branch, `MenuTabs_clampWindow`, `pushToolsOverTab`.
- D2: `MenuTabs_reload(keep, what)`; `refreshInputs(plan)` behind `static MenuTabInputs last_in`; a current root is
  kept only if next==current ∧ not stale ∧ path matches ∧ (Consoles: `root_gen == Content_libraryGen()`), then
  `ListWindow_reload` re-windows it; `dropSlot(next)` before a rebuild; row count read after `current = next`.
  Masks: Refresh/Delete ROM ALL; Pin/Unpin PINS; Rename ROM PINS|RECENTS; Rename/Delete collection PINS|COLLECTIONS.
  Theoretical gap noted: a ROM file named exactly like a collection (`X.txt`) would alias that collection's row.
- H: ids 2/40/41 left inline (single calls); three `break`→`return`; all helpers `void`, no shared state.
Device: Brick full OTA 03:39 (every app rebuilt, 0 errors), harness 03:43 GREEN (elf md5 cc05eb5c… = local build,
`stats: cache hit`, 12 tab switches = 0 I/O, Settings open/quit via B with the rebuilt api.c, no crash); Home/Tools/
Settings screenshots match the pre-refactor ones. Manual checks still owed (see each chunk's list above): held D-pad
edge/wrap, L/R page, letter jump, collection rename highlight, every context action once, Grid↔Carousel kinds, Vertical
stacks. SPS off adb → not updated since the Tier-2 build.

**2026-10-03 03:13 — Ports art regression (user report, not from the review), FIXED, UNCOMMITTED:** commit d28e69cd
("retire the mix and game-art settings") replaced `ROM_displayArtPath(path, CFG_getEffectiveArtType(), fallback_to_mix
= true, …)` with hard-coded `ART_TYPE_SCREENSHOT, false` in the List thumbnail (gamelist.c), `entryHasArt` and HomeArt's
`screenshotFor`, so art living only at the root `.media/<name>.png` (PortMaster's convention, hand-made art, older
scrapes) was never found — Ports showed placeholders and the Home Continue card for a Port had no picture. Fix: new
`ROM_findScreenshot()` in common/utils.c/h (`.media/screenshot/<name>.png`, else root `.media/<name>.png`, else miss
with out = the screenshot path) used by those three sites; box art stays strict (`.media/boxart/` only, placeholder box
otherwise). Host test `test_art_path.c` (utils.c compiled on the host with the tg5040 platform include;
`-Wno-deprecated-declarations` for macOS sprintf). Brick-verified by screenshot (StardewValley card has its art);
SPS pending (off adb).

**2026-10-03 02:56 — BRICK (tg5040, `5c000c8997414781d1d`) DEVICE-VERIFIED by the automated harness** after the full
OTA (`ANDROID_SERIAL=… make deploy DEVICE=brick`, MinUI-brick.zip 251,450,205 B; card was on main dbfdc9fc → now
main-menu-tabs 0c2c5d9a + the uncommitted tree; running elf md5 `743fb3cd5f82380389b5eaa8c34376ea` = local
build/tg5040). Results: 1 fingerprint/boot, `stats: cache miss, computing` on first Home, 12 injected L1/R1 switches →
0 new fingerprint lines + PID stable, Settings launched via /tmp/next and quit with physical B (Nintendo layout) →
nextui back, settings log `UIFont: 1 font opens` at quit, no crash markers, 16-row 3-column emulist cache; screenshots
Home/Tools/Settings at 1024×768 all correct. The Brick card has no collections (only map.txt) → no Collections tab,
so T2-5/6 were not exercised there. Harness: scratchpad `bricktest.sh` (device, evdev event3 + screenshot.elf daemon
via `trap '' HUP`) + `run_bricktest.sh` (host driver); same shape as the SPS probes (event4). Reusable for the next
deploy — copy into `.dev/` if wanted.

**2026-10-03 02:51 — USER-TESTED on the Smart Pro S: "seems good" (Tier 1 + Tier 2 build, md5 `4dbd8182…`).** Brick
full OTA deploy (`make deploy DEVICE=brick`, the card was on main dbfdc9fc so a loose elf was not safe) started 02:48.

**Deploy state 2026-10-03 01:10 (Tier 2):** Smart Pro S runs the full Tier-1 + Tier-2 tree as a loose `nextui.elf`
(md5 `4dbd81822d99c1a7635f7b3631a7eafa`). Host suite 34 green (new: `collcount_stamp_cache`, `test_home_stats_model`
cases, `bilinear_matches_old`), nextui tg5040 + tg5050 and settings tg5050 compile clean. Device: boots into Home →
exactly one `stats: cache miss, computing` → `.minui/home_stats.txt` written; Collections visit → `collection_counts.txt`
written once (one-time recount after the v6 tag); `UIFont:` counter never reached 50 opens; no crash markers;
1 fingerprint/boot. Brick (tg5040) compile-only. Manual checks owed for Tier 2: collection add/rename/delete updates the
count at once; delete a ROM that is in a collection → count drops; launch within 1 s of first Collections visit → counts
persisted; X/resume hint follows selection after Search/switcher; Tools hidden → root context Tools → launch → return;
Backdrop art faster when scrolling fast; placeholders appear in `.minui/placeholders` after idling, no `.tmp` left;
Game Tracker thumbnails all round; Settings + a tool open/quit cleanly at native and non-native UI scale; after a game
Home shows `stats: cache miss` and updated time; `UIFont:` opens settle after a long scroll at a larger UI scale.

**Deploy state 2026-10-03 00:33 (Tier 1):** Smart Pro S (`7057408880c2c8c239a`) runs the full Tier-1 tree as a loose
`nextui.elf` push (md5 `aa08567bbe82c372339a0d8243bd027b`) over its 668ca8da card at
`/mnt/SDCARD/.system/bin/nextui.elf`; its card's emulist/romindex caches are now `romindex-v6`. The Brick was
not attached (tg5040 compile-only). Tree: 24 files, +870/−309, all UNCOMMITTED on `main-menu-tabs`.
Manual checks still owed (human at the screen): held D-pad in Carousel/Grid smooth and ahead-built; Home revisit
without a hitch; Fetch artwork in Carousel/Grid/Home → new art at once; Consoles › only-console delete last ROM →
land on next tab, B inert; Add to Collection › New then visit Collections; rename a ROM then Search finds it;
change layout/scale then switch tabs (window re-clamps); pin/unpin from another tab then Home shows it.

Record what was done, by whom/when, commit SHA, and device verification (Brick = tg5040, SPS =
tg5050; Brick Pro is reserved by another session as of 2026-09-30 — don't deploy there).

| ID | Status | Commit | Verified | Notes |
|---|---|---|---|---|
| T1-1 cached root per tab | DONE 2026-10-03 (uncommitted, plan steps 5+7) | — | SPS (tg5050) DEVICE-VERIFIED 00:33: 12 injected L1/R1 switches → 0 new fingerprint lines, PID stable, screenshots Home→Consoles OK; Brick (tg5040) compile-only; manual feel/edge checks pending | `MenuTabSlot slots[]` replaces `TabMemory`; `openRootKeepFocus` parks the outgoing plain root / takes the incoming one (no card I/O), re-clamps the window with `GameList_rowCountAt(true)`; Consoles root re-validated via `root_gen`/`consoles_gen` vs `Content_libraryGen()`; `MenuTabs_reload` drops all slots, `MenuTabs_setCurrent` drops one, new `MenuTabs_dropCached` (Add-to-Collection new branch) + `MenuTabs_quit` (Menu_quit). Behaviour change: `MenuTabs_openRoot` on the current tab reuses its root. |
| T1-2 Home card cache / rebuild key | DONE 2026-10-03 (uncommitted) | — | both compiles clean; SPS boots to a fully rendered Home (screenshot 00:33); hitch-free revisit needs a human eye | `cardCacheClear()` out of `rebuild()`; `built_gen` dropped from the rebuild key (serial + screen + scale + `need_rebuild` only); `cardStamp` now also hashes lit accent colours, `cont_preview`, and `layout.mode/card.w/card.h` for the Stats card. Bonus: `gameswitcher.c` Y-remove now calls `Home_reset()` (pre-existing gap: Continue kept showing a removed recent). |
| T1-3 prefetch off the render pass | DONE 2026-10-03 (uncommitted) | — | both compiles clean; SPS runs it (Consoles carousel renders, no crash across 12 switches + 2 reboots); held-D-pad smoothness needs a human eye | New `RowView_prefetchStep` / `StackView_prefetchStep` / `GridView_prefetchStep(Uint32 deadline)` + dispatcher `GameList_prefetchIdle(deadline)`; each view keeps `pf {armed, geometry, n, sel}` from its last render and disarms on any identity change. nextui.c calls it (1) after `GFX_flip` with the frame's leftover (16 − 2 − work_ms) and (2) in the idle branch with an 8 ms slice, forcing the 16 ms cadence while work remains. `prefetch_pending`, `item_builds`/`RowView_itemBuilds`, `tile_composes` removed from all three views and from `*_animating()`. `PREFETCH_SLIDE_SHARE 0.6f` + `slide_retargeted`/`lit_retargeted` so held scrolling (100 ms repeat vs 260/300 ms tween) builds ahead. Watch on device: a single expensive compose can still overrun the deadline; Backdrop gains less (`RowView_pictureBusy` gate kept). |
| T1-4 startup double scan + ROM index read | DONE 2026-10-03 (uncommitted, plan steps 1-4) | — | SPS DEVICE-VERIFIED: first boot after upgrade rescanned once and wrote the 3-column emulist (`Arcade (FBN)\tArcade\t2`); warm reboot = exactly 1 fingerprint line, cache mtimes untouched, 17.7 s boot; Consoles carousel shows the counts (2 games / 1 game). Brick compile-only | New `emulist_model.c/.h` (`Emulist_collatedPrefix/collates/parseRow/formatRow/collateCounts/adjustCount`); content.c memo (`emu_memo`, `emu_counts`, `emu_fp`, `lib_gen`) filled once by `ensureEmulist()`, `getRoms()` returns a copy; `Content_consoleCount()`, `Content_libraryGen()`; emulist cache now `path\tname\tcount`, `CACHE_SCHEMA_TAG` = `romindex-v6` (one rescan on upgrade); `Content_forgetRom` lowers counts via `Emulist_adjustCount`; `buildConsoleCounts` & co deleted; Search drops the memo when the romindex is stale. Deviations: `lib_gen` bumps on drop too; `scanRoms` reuses the fingerprint; `Content_consoleGameCount`/`libraryFingerprint` call `ensureEmulist()` on an empty memo; `LOG_info("content: rom cache fingerprint")` added for the device check. |
| C-1 carousel tile key after fetch | DONE 2026-10-03 (uncommitted, plan step 8) | — | both compiles clean; SPS runs it; Fetch-artwork refresh needs a human eye | `HomeArt_lastGen()` (gen of the slot the last `lookup` returned, UI thread, read right after the call). Gen added beside the existing pointer in: rowview `carouselTile` key (`…|state|%u|path|name`), rowview `sideArt` key (`A|w|h|%p|%u|path`, new `gen` param), gridview `TileSpec.picture_gen` hashed in `tileStamp`, home.c `artStamp(h, st, pic, gen)`. Device check: Fetch artwork in Carousel / Grid / Home Continue + pin / Backdrop neighbour → new art at once. |
| C-2a reload leaves foreign sub-stack | DONE 2026-10-03 (uncommitted, plan step 6) | — | both compiles clean; SPS runs it; manual check pending (delete last ROM of the only console) | `MenuTabs_reload`: when `next != old_tab` pop `stack[1..]`, `top = fresh`, `MenuTabs_leaveFocus()`. |
| C-2b … C-2j | TODO | | | after Tier 1; C-2d/e/f overlap T2 items |
| T2-5/C-2f CollCount stat cache | DONE 2026-10-03 01:04 (uncommitted) | — | run_tests.sh green incl. new `collcount_stamp_cache`; device pending | Pure stamp table in collcount_model (`CollCount_stampFind/Put/stampsFree`), UI-thread only; a file is re-stat'd only when its stamp generation is older than `stamp_gen + reports + MenuTabs_generation()` (invalidate / worker report / tab switch). Library fingerprint still checked per call (memo). Gap fixed: `removeCollectionLines` (ROM delete pruning collections) now calls `CollCount_invalidate` per changed file. |
| T2-6 CollCount batched fsync + pre-launch flush | DONE 2026-10-03 (uncommitted) | — | tests green; device pending (collection_counts.txt written once ~0.5 s after counts settle; launch within 1 s of first Collections visit still persists) | Worker writes once the queue is idle 500 ms (`FLUSH_IDLE_MS`) or at most every 5 s while busy (`FLUSH_BUSY_MS`) via one `persist()` (new `write_lock` taken before `lock`, snapshot under `lock`, write outside). `CollCount_flush()` called from launcher.c `queueNext()` (covers every `_exit` launch incl. `autoResume`/`openScript`, which never call `saveLast`); `CollCount_quit` still flushes. |
| T2-8 readyResume keyed on selection | DONE 2026-10-03 (uncommitted) | — | syntax-clean; device: X/resume hint follows selection, correct after Search/switcher | `readySelectedResume(entry)` in gamelist.c skips the probe when (serial, selected, generation, entry path, `readyResumeCount()`) are unchanged; `readyResumeCount()` in launcher.c increments on every `readyResume` so a probe by Search/switcher/Home/context action forces a re-probe. Replaces the per-dirty-frame call and the Grid/Row step calls. |
| C-2g pushToolsOverTab dedupe | DONE 2026-10-03 (uncommitted) | — | syntax-clean; device: Tools hidden → root context Tools → launch tool → return lands on that row, B returns to the tab | One `pushToolsOverTab(const char* select_path)` in launcher.c/h used by `loadLast` and the context menu; kept window-clamped-to-selection (`MenuTabs_clampWindow`) and `MenuTabs_leaveFocus` for both; resume probe stays with the caller. |
| T2-7/C-2e HomeStats lazy + persisted | DONE 2026-10-03 01:03 (uncommitted) | — | model test green (round-trip, every key field, malformed); device pending | `Home_reset()` no longer requests stats; request from Home's show path; worker builds `HomeStatsKey` {day, signed_in, game_logs.sqlite (m,s), .ra/pending/unlocks.jsonl + confirmed.jsonl (m,s), .ra/cache/games + sessions (dir mtime, entry count)} and reuses in-memory result or `SHARED_USERDATA_PATH/.minui/home_stats.txt` (`homestats 1` text format) on a key match, logs `stats: cache hit|miss`. Requested on the false→true edge of `Home_active()` (boot into Home, tab step onto Home, pop back to Home) — not on pins from inside Home; midnight covered by `key.day` (test `day_rolls_at_local_midnight`). Stale window: a sets.json rewritten in place with no other key change, until midnight or the next Home show with any other key change. |
| T2-9 per-frame text measuring | DONE 2026-10-03 01:05 (uncommitted) | — | syntax-clean; device: logo-less console names/counts and tab row look identical | rowview `logoNameHeight()` 16-slot memo (name, slot w/h, content scale k, FIXED_SCALE), used by `logoCountTop`/`drawItemCount`/`drawSideCount`, cleared in `itemsClear()`; menutabs `layoutLabels()` keeps last x/width keyed on (font ptr, tab count, tab ids, FIXED_SCALE). `Content_consoleGameCount` part already gone via T1-4. |
| T2-10 tile text LRU + font cache | DONE 2026-10-03 (uncommitted) — single-face design REJECTED | — | device: grep log for `UIFont:` after scrolling Grid/Carousel/Backdrop at a larger UI scale; opens should settle (if it climbs past 64 the working set is still too big) | tiles.c `blitTextColor` → `GFX_renderText` + free (pixels identical; menutabs keeps `GFX_getCachedText` as a per-frame user). Font cache: tg5040 toolchain ships SDL_ttf **2.0.13** (no `TTF_SetFontSize`), tg5050 2.0.18 → kept per-size faces, `UIFONT_MAX` 32→64, sizes NOT quantised (would change fractional/px sizes visibly), opens counter logged every 50 + at `UIFont_quit`. Follow-up: `InfoBand_drawSegments` (via `buildCaption`) may have the same LRU pattern — not checked. |
| T2-14 itemFind hash | DONE 2026-10-03 (uncommitted) | — | — | `ItemSlot.hash` FNV-1a of the stored (possibly truncated) key; `itemFind` compares hash before `strcmp`. |
| C-2c GFX_quit leaks | DONE 2026-10-03 (uncommitted) | — | device: open Settings + a tool (ratools), quit, at native and non-native UI scale on both devices | `GFX_quit` clears the text cache, `hwNativeFree()` (3 fonts + sheet, with `GFX_forgetFontText` first), closes `font.xlarge`/`font.title` (also leaked), all `font_ar.*`, `hint_tiny_native_ar`; NULLs everything for a later `GFX_init`. `GFX_loadSystemFont` opens `hint_tiny_native(_ar)` only when `NATIVE_SCALE != FIXED_SCALE`. |
| T2-12 HomeArt worker scaling/PNG/cancel/pin | DONE 2026-10-03 01:02 (uncommitted) | — | host: `bilinear_matches_old` test (≤2/channel vs old scalar, NEON + plain C both pass under ASan); bench 1280×720 fill 7.6 ms → 1.0 ms; device pending | `AreaScale_bilinearCover()` (area_scale.c/.h; `AreaScale_argb` was unsuitable: float premultiplied box filter, differs when enlarging) replaces `cropFill`'s `lerpPixel`; `halve` + Backdrop dim pass two-channels-per-word (bit-identical); placeholder PNGs queued (≤8) and written only when no decode waits, flushed in `HomeArt_quit`, queued-but-unwritten placeholder reused; worker re-checks slot evicted/forgotten after decode and after scale; `PWR_pinHelperThread()` in `workerMain` (no-op in nextui until a helper core set is recorded). |
| C-2b gametime round thumbnails | DONE 2026-10-03 (uncommitted) — finding was mostly WRONG | — | syntax-clean | Committed code already converted to RGBA32 (has alpha), so 24-bit art was round already. Real gaps fixed: NULL deref if the conversion failed, unchecked `CreateRGBSurface`; now ARGB8888 with NULL checks, `maskCircle` refuses non-32-bit/no-alpha with a `LOG_warn`. |
| T2-11 reload "what changed" mask | DEFERRED | | | acceptable as is; menutabs.c |
| T2-13 Backdrop frame cost | DEFERRED | | | profiling-gated |
| C-2h duplicated tween/kind helpers | DEFERRED → refactor wave | | | refactor item 3 |
| C-2i GFX_blitHardwareGroup global swap | DEFERRED → refactor wave | | | api.c |
| C-2d MenuTabs_init cost on reload | PARTLY by T1-4 (`Content_consoleCount`) | | | Recents_load/Shortcuts_validate remain per reload = T2-11 |
| C-2j collation helper | DONE by T1-4 (`Emulist_collates`) | | | |

Build/test commands (from the worktree root): `make -C workspace/all/common/tests` for the host
tests (see `.dev/BUILD.md` for the platform builds and `.dev/DEVICES.md` / memory `device-deploy` for
the Brick/SPS deploy recipe). The user commits personally — don't commit unless asked.
