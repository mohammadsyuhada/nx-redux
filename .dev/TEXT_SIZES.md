# Text sizes per device

A log of the by-eye text-size tuning: the UI is tuned page by page on each
device, one device at a time, so each device has its own sizes. When the
devices' values follow a pattern (the same ratio across pages), they can be
folded back into the device's UI scale (`ui_scale.h`).

The values live in `workspace/all/common/ui_text_sizes.h` (one `TEXT_*` entry
per tuned element, px per device). Anything not listed there still follows the
device's UI scale.

## Devices

| Device | Platform | Resolution | Screen | Density | UI scale |
|---|---|---|---|---|---|
| Brick | tg5040 | 1024×768 (4:3) | 3.2" | ~400 ppi | 3.0 |
| Brick Pro | tg5040 | 1024×768 (4:3) | 3.95" | ~324 ppi | 2.5 (density gives 2.4375; 2.5 chosen 2026-10-07 for rounder sizes) |
| Smart Pro / Smart Pro S | tg5040 / tg5050 | 1280×720 (16:9) | 4.96" | ~296 ppi | 2.25 (density seed) |

The Brick and the Brick Pro share a resolution but not a screen size, so the
same pixel size reads about 1.23× bigger on the Brick Pro.

## Order

1. Brick — **done** (review completed 2026-10-07)
2. Brick Pro — **done** (review completed 2026-10-07)
3. Smart Pro S — **done** (review completed 2026-10-07)

## Adjustments

Sizes are the text's height in screen px (the TTF point size the font opens at).
"Before" is what the UI scale gave it.

| Date | Device | Page | Element | `ui_text_sizes.h` | Before | After | Notes |
|---|---|---|---|---|---|---|---|
| 2026-10-07 | Brick | Home | Stats strip ("This month …", "Most played …") | `TEXT_HOME_STATS` | 27 | 30 | "a bit bigger"; the two lines' spacing and the cards below move down with it (`HomeLayout_computeStripText`) |
| 2026-10-07 | Brick | Home | Stats strip | `TEXT_HOME_STATS` | 30 | 33 | "one size bigger" (sizes step by 3 px). **Accepted.** |
| 2026-10-07 | Brick | Home | Continue card: game title | `TEXT_HOME_CONT_TITLE` | 29 | 26 (tuned) | "same as the stats" |
| 2026-10-07 | Brick | Home | Continue card: info line (time played) | `TEXT_HOME_CONT_INFO` | 21 | 26 (tuned) | "same as the stats, including the extra infos"; both baselines move up with it (`HomeLayout_captionBaselines`): info 21→33 px above the card's bottom, title 49→77 (44 px apart, as the stats lines). **Accepted** (with the title). |
| 2026-10-07 | Brick | Home | Pinned games: card height | `HOME_PIN_K` | 1× | 2× | "increase the height of the card to double"; the pins sit below the screen's end, so the page just scrolls further (`HomeLayoutOpts.pin_k`) |
| 2026-10-07 | Brick | Home | Pinned games: game name | `TEXT_HOME_PIN_NAME` | 23 | 26 (tuned) | "33px as the Continue card too"; its line box grows with it (28→40) |
| 2026-10-07 | Brick | Home | Pinned games: info line (time, on the selected pin) | `TEXT_HOME_PIN_INFO` | 19 | 26 (tuned) | as the name; line box 24→42 |
| 2026-10-07 | Brick | Home | Pinned games: game name | — | 1 line, "…" | up to 2 lines, "…" on the 2nd | "when the title is too long, wrap it to 2 lines"; the extra line stacks upward. Behaviour, so every device gets it. **Accepted** (Home done). |
| 2026-10-07 | Brick | Main menu, List layout (Consoles tab) | Rows: text | `TEXT_MENU_LIST` | 48 | 41 (test) → **reverted** to 48 | "basically perfect size", but test a size that fits 7 rows instead of 6. Today's text/row ratio (48/89) on the 7-row height |
| 2026-10-07 | Brick | Main menu, List layout | Rows: pitch | `MENU_LIST_PITCH` | 90 (6 rows of 89) | 80 (7 rows of 76) → **reverted** to 90 | the list fits floor(slot / 0.95 pitch) rows of slot/n px (slot 534); the selection pill takes the row's 76 px, its caps drawn (`ui_pill_cap.h`) since the 90 px sheet's can't shrink. Game lists unchanged |
| 2026-10-07 | all | Lists (main menu and game lists) | Scroll arrows (info band) | `ARROW_ALPHA` (infoband.c) | 20% white | 45% white (60%, then 40% tried) | nearly invisible: no fade behind the band since it was removed, and they sit at the hint bar's top edge. Not per device. **Accepted** (45%; the hidden-page-title top arrow shares it). |
| 2026-10-07 | all | Lists | Scroll arrows: position | `INFOBAND_ARROW_RAISE` (infoband.h) | centred on the band's text line | 3 dp higher (6 px on the Brick) | "move it up a few pixel", off the hint bar's top edge; the hidden-page-title up arrow above the first row mirrors it |
| 2026-10-07 | all | Every list's scroll arrows (Settings, tools, PIN dialog, main menu band) | Opacity | `UI_SCROLL_ARROW_ALPHA` (ui_list.h) | Settings etc.: the sheet's dark grey (~15%) | 45% white, as the main menu | "use this arrow opacity for all the same arrow"; one shared arrow (`UI_scrollArrow`). The main menu's band also drew its 45% as 20% (blended onto its layer, then the layer blended again); fixed by copying the band onto the layer |
| 2026-10-07 | Brick | Apps' main lists (Settings categories, tools' menus) | Rows | `MENU_LIST_ROWS` | as many as fit (6) | 7 (test) → **reverted** to 0 | tried with the main menu's 7 rows; reverted the same day |
| 2026-10-07 | Brick | Game list, List layout | Info line (play time, achievements; on the hint bar's row while the hints are hidden) | `TEXT_LIST_INFO` | 36 (font.small) | 26 (tuned) | "same size as the stats in Home"; centred where the 36 px line sat. The band's own line (Collections' "N games") keeps font.small |
| 2026-10-07 | all | Collections, List layout | "N games" line in the bottom band | — | shown | removed | not sizing: "remove the 'N games' line in the collections with list layout". Grid/Carousel/Backdrop keep their counts |
| 2026-10-07 | all | Consoles, Grid layout | Logo + "N games" | — | logo centred, count hung under it (tall logos pushed it onto the border) | logo and count centred as one block; the logo's box gives up the count's line (always reserved, so nothing moves when a count arrives) | not sizing (`GridLayout_logoBlock`, `Tiles_gridLogoBox`) |
| 2026-10-07 | Brick | Tools, Grid layout | Tool name | `TEXT_GRID_TOOL` | 31 (14 sp × tile scale) | 26 (tuned) | "same as the stats size"; still shrinks to fit a long word (to 11 sp) |
| 2026-10-07 | Brick | Tools, Grid layout | Tile width | `GRID_TOOLS_W_K` | 1× the spec shape | 1.5× (height unchanged) | "the width to 1.5x current size" |
| 2026-10-07 | Brick | Collections, Carousel (Horizontal) | Collection name | `TEXT_CAROUSEL_COLL_NAME` | 36 sp × the slot scale (shrinks to fit) | starts at 33 px (still shrinks to fit its longest word) | "increase the collection name … to the stats size" |
| 2026-10-07 | Brick | Collections and Consoles, Carousel (Horizontal) | "N games" | `TEXT_CAROUSEL_COUNT` | 14 sp × the slot scale | 26 (tuned) | "and the 'N games' to the stats size … the same for consoles". Vertical stacks unchanged |
| 2026-10-07 | Brick | Collections, Carousel (Horizontal) | Collection name | `TEXT_CAROUSEL_COLL_NAME` | 33 (came out the count's size) | 42 | "collection name, increase it" |
| 2026-10-07 | Brick | Collections, Carousel (Horizontal) | Slot width | `CAROUSEL_COLL_W_K` | 1× (300 dp spec) | 1.5× (height unchanged) | "increase the width of the item to 1.5x" |
| 2026-10-07 | Brick | Collections, Carousel (Horizontal) | Collection name | `TEXT_CAROUSEL_COLL_NAME` | 42 | 48 | "increase it 2 more step" (2 × 3 px). **Accepted.** |
| 2026-10-07 | Brick | Tools, Carousel (Horizontal) | Tool name | `TEXT_CAROUSEL_TOOL` | 17 sp × the slot scale | 33 (still shrinks to fit its longest word) | "the same as the one in grid size" |
| 2026-10-07 | Brick | Tools, Carousel (Horizontal) | Tool name | `TEXT_CAROUSEL_TOOL` | 33 | 36 (test) | "test up one more size for the name" |
| 2026-10-07 | Brick | Tools, Carousel (Horizontal) | Slot width | `CAROUSEL_TOOL_W_K` | 1× | 1.5× (height, icon unchanged) | "increase the slot width" (1.5×, as Collections) |
| 2026-10-07 | Brick | Tools, Carousel (Horizontal) | Slot width | `CAROUSEL_TOOL_W_K` | 1.5× | 1.25× | "the gap is too large, not like others" (between items: 326 px centre to centre at 1.5×, ~274 at 1.25×, 222 at 1×) |
| 2026-10-07 | all | Tools and Collections, Carousel (Horizontal) | Item spacing | `Row_itemDxVar`, `slotContentPx` (rowview.c) | every slot the same width (long and short names alike: uneven gaps) | each item as wide as its content (icon/name, or name lines/count), one gap between neighbours' edges; `*_W_K` now the widest a slot may be (Tools back to 1.5×) | "don't limit the slot width … use the space it needs, but keep the space between items the same". Not sizing; equal widths move exactly as before |
| 2026-10-07 | all | Collections, Carousel (Horizontal) | Name line breaks | `Row_collBreak` (row_model.c) | wrapped to the slot | a word longer than 5 characters starts a new line (never the first word; 2 lines at most) | "always break a new word (word with more than 5 characters to a new line)". Vertical stacks unchanged |
| 2026-10-07 | Brick | Tools and Collections, Carousel (Horizontal) | Gap between items | `CAROUSEL_ITEM_GAP_K` | 1× (14 dp) | 2× (28 dp) | "okay, but add some more gap between items". **Accepted** (with content-sized slots, line breaks, 36 px tool names). |
| 2026-10-07 | Brick | Game list, Carousel (Horizontal) | Game name (caption under the row) | `TEXT_CAROUSEL_GAME_NAME` | 46 (18 sp) | 43 | "reduce the game name one step". Backdrop's caption unchanged |
| 2026-10-07 | Brick | Game list, Carousel (Horizontal) | Game name | `TEXT_CAROUSEL_GAME_NAME` | 43 | 40 | "reduce one more step for the name" |
| 2026-10-07 | Brick | Game list, Carousel (Horizontal) | Info rows (play time, achievements) | `TEXT_CAROUSEL_GAME_INFO` | 36 (14 sp) | 26 (tuned) | "for the stats info, use our stats font" |
| 2026-10-07 | Brick | Game list, Carousel (Horizontal) | Selected picture | `CAROUSEL_GAME_SEL_K` | 1× (340 × 240 dp × screen factor) | 1.1× (neighbours kept at their size: their scale ÷ 1.1) | "increase the selected image a little bit" |
| 2026-10-07 | Brick | Game list, Carousel (Horizontal) | Selected picture | `CAROUSEL_GAME_SEL_K` | 1.1× | 1.2× | "add 10% more" |
| 2026-10-07 | Brick | Game list, Carousel (Horizontal) | Selected picture | `CAROUSEL_GAME_SEL_K` | 1.2× | 1.4× (≈ 508 × 360 px, was 363 × 257) | "make it 1.4x" |
| 2026-10-07 | all | Game list, Carousel (Horizontal) | Vertical placement | `carouselItemCy` (rowview.c) | the row and a reserved caption block centred together (every item on one line) | the neighbours on the body's centre; the selection's picture and its actual caption (as many rows as that game has) centred as one block, easing between the two as it moves | "vertically align the selected carousel … only the selected slot will move up/down according to the stats rows". Not sizing |
| 2026-10-07 | Brick, Brick Pro | Game list, Carousel (Horizontal) | Caption lines | `CAROUSEL_CAPTION_INFO_LINES` | title + one info line (time · trophies · Next) | title; time · trophies; Next — each info line only when it has something | "the extra info rows should be 3 rows: title, playtime if available, next achievement if available". The Smart Pro keeps one info line |
| 2026-10-07 | Brick | Game list, Carousel (Vertical) | Caption beside the stack: name and info | `TEXT_CAROUSEL_GAME_NAME`, `TEXT_CAROUSEL_GAME_INFO` | 41 (16 sp) and 36 (14 sp) | 40 and 33, as the horizontal Carousel | "the game title and stats resize to the vertical carousel too". Backdrop-Vertical unchanged |
| 2026-10-07 | all | Game list, Carousel (Vertical) | Caption beside the stack: name lines | — | 2 at most | 3 at most ("…" on the third) | "if the name is too long, it can wrap to 3 rows max". Backdrop-Vertical keeps 2 |
| 2026-10-07 | Brick | Game list, Backdrop (Horizontal and Vertical) | Caption: name and info | `TEXT_CAROUSEL_GAME_NAME`, `TEXT_CAROUSEL_GAME_INFO` | 46 / 41 (side) and 36 | 40 and 33, as the Carousel | "also the game name and the stats text size" |
| 2026-10-07 | all | Game list, Backdrop (Horizontal) | Vertical placement | `carouselItemCy` | row and reserved caption centred together | as the Carousel: side boxes on the body's centre, the selection with its actual caption centred | "do the vertical adjustment like the carousel to the backdrop too" |
| 2026-10-07 | Brick, Brick Pro | Game list, Backdrop (Horizontal) | Caption lines | `CAROUSEL_CAPTION_INFO_LINES` | title; time; trophies · Next | title; time · trophies; Next (each only when it has something) | "yes do the 3 rows too". The Smart Pro keeps Backdrop's rows |
| 2026-10-07 | all | Settings > Layouts | Page title default | `CFG_DEFAULT_PAGETITLE` (config.h) | Hide | Show (as the docs already said) | "the tab/page title should be visible by default". A saved `pagetitle=` keeps its value |
| 2026-10-07 | Brick Pro | Home | Stats strip | `TEXT_HOME_STATS` | 22 | 25 | "increase it one step" (the Brick Pro pass starts: scale 2.4375) |
| 2026-10-07 | Brick Pro | Home | Stats strip | `TEXT_HOME_STATS` | 25 | 28 | "add one more step". **Accepted.** |
| 2026-10-07 | Brick Pro | Home | Continue card: title | `TEXT_HOME_CONT_TITLE` | 24 | 28 | "same text size as stats" |
| 2026-10-07 | Brick Pro | Home | Continue card: info line | `TEXT_HOME_CONT_INFO` | 17 | 28 | "same text size as stats"; baselines move up with it (`HomeLayout_captionBaselines`). **Accepted** (with the title). |
| 2026-10-07 | Brick Pro | Home | Pinned games: name | `TEXT_HOME_PIN_NAME` | 18 | 28 | "same as the stats size"; up to 2 lines |
| 2026-10-07 | Brick Pro | Home | Pinned games: info line | `TEXT_HOME_PIN_INFO` | 15 | 28 | "same as the stats size" |
| 2026-10-07 | Brick Pro | Home | Tools: squares per column | `HOME_TOOL_ROWS` (`HomeLayoutOpts.tool_rows`) | 4 | 5 (smaller squares, same height) | not text: "instead of 4 items per column, set it to 5, only for Brick Pro" |
| 2026-10-07 | Brick Pro | (whole UI) | UI scale | `UIScale_forDevice` | 2.4375 | 2.5 | "2.4375x seems too complex; switch to 2.5x". Sheets baked at 2.5 / 1.6875 (2.4375 / 1.625 removed). The untuned UI-scale-derived entries follow: `TEXT_MENU_LIST` 39→40, `MENU_LIST_PITCH` 73→75, `TEXT_LIST_INFO` 29→30. Explicit Home sizes unchanged. Also fixed: `GFX_blitRectColor` left a 1 px gap when a sheet piece is odd-sized |
| 2026-10-07 | Brick Pro | Main menu, List layout (Consoles) | Rows | `TEXT_MENU_LIST`, `MENU_LIST_PITCH` | — | 40 px on a 75 px pitch (the 2.5 scale's) | "current setup is ok". **Accepted** as is |
| 2026-10-07 | Brick Pro | Main menu, List layout | Rows | — | 7 rows | 7 rows kept | "keep 7 rows" (the larger panel fits one more row at the same physical size) |
| 2026-10-07 | Brick Pro | Game list, List layout | Info line (hints hidden) | `TEXT_LIST_INFO` | 30 (font.small at 2.5) | 28 | "use the same stats size" |
| 2026-10-07 | Brick Pro | Tools, Grid layout | Tool name | `TEXT_GRID_TOOL` | 14 sp × tile scale | 28 | "everything the Brick set to the stats size, the same on the Brick Pro" (the Brick's 33 = the stats; the Brick Pro's stats are 28) |
| 2026-10-07 | Brick Pro | Collections and Consoles, Carousel (Horizontal) | "N games" | `TEXT_CAROUSEL_COUNT` | 14 sp × slot scale | 28 | as above |
| 2026-10-07 | Brick Pro | Game list, Carousel and Backdrop | Caption info rows | `TEXT_CAROUSEL_GAME_INFO` | 14 sp (35) | 28 | as above |
| 2026-10-07 | Brick Pro | Collections, Carousel (Horizontal) | Collection name | `TEXT_CAROUSEL_COLL_NAME` | 36 sp × slot scale | 41 | "carry them over proportionally": the Brick's 48 × 28/33 |
| 2026-10-07 | Brick Pro | Tools, Carousel (Horizontal) | Tool name | `TEXT_CAROUSEL_TOOL` | 17 sp × slot scale | 31 | the Brick's 36 × 28/33 |
| 2026-10-07 | Brick Pro | Game list, Carousel and Backdrop | Game name | `TEXT_CAROUSEL_GAME_NAME` | 18 sp (46) / 16 sp beside a stack | 34 | the Brick's 40 × 28/33 |
| 2026-10-07 | Brick Pro | (layouts) | Multipliers | `HOME_PIN_K` 2×, `GRID_TOOLS_W_K` 1.5×, `CAROUSEL_COLL_W_K` 1.5×, `CAROUSEL_TOOL_W_K` 1.5×, `CAROUSEL_ITEM_GAP_K` 2×, `CAROUSEL_GAME_SEL_K` 1.4× | 1× | as the Brick | "apply them too" (the Brick's layout multipliers, 1:1) |
| 2026-10-07 | Brick Pro | Home | Pinned games: height | `HOME_PIN_K` | 2× | 1× | "reduce back to half of current size" |
| 2026-10-07 | Smart Pro S | Home | Stats strip | `TEXT_HOME_STATS` | 20 | 23 | "increase one step" (the Smart Pro S pass starts: scale 2.25) |
| 2026-10-07 | Smart Pro S | Home | Stats strip | `TEXT_HOME_STATS` | 23 | 26 | "add one more step". **Accepted.** |
| 2026-10-07 | Smart Pro S | (stats-size entries) | TEXT_HOME_CONT_TITLE | `TEXT_HOME_CONT_TITLE` | 22 | 26 | "carry the stats size over to others" (the Brick's 33 entries) |
| 2026-10-07 | Smart Pro S | (stats-size entries) | TEXT_HOME_CONT_INFO | `TEXT_HOME_CONT_INFO` | 16 | 26 | "carry the stats size over to others" (the Brick's 33 entries) |
| 2026-10-07 | Smart Pro S | (stats-size entries) | TEXT_HOME_PIN_NAME | `TEXT_HOME_PIN_NAME` | 17 | 26 | "carry the stats size over to others" (the Brick's 33 entries) |
| 2026-10-07 | Smart Pro S | (stats-size entries) | TEXT_HOME_PIN_INFO | `TEXT_HOME_PIN_INFO` | 14 | 26 | "carry the stats size over to others" (the Brick's 33 entries) |
| 2026-10-07 | Smart Pro S | (stats-size entries) | TEXT_LIST_INFO | `TEXT_LIST_INFO` | 27 | 26 | "carry the stats size over to others" (the Brick's 33 entries) |
| 2026-10-07 | Smart Pro S | (stats-size entries) | TEXT_GRID_TOOL | `TEXT_GRID_TOOL` | UI scale | 26 | "carry the stats size over to others" (the Brick's 33 entries) |
| 2026-10-07 | Smart Pro S | (stats-size entries) | TEXT_CAROUSEL_COUNT | `TEXT_CAROUSEL_COUNT` | UI scale | 26 | "carry the stats size over to others" (the Brick's 33 entries) |
| 2026-10-07 | Smart Pro S | (stats-size entries) | TEXT_CAROUSEL_GAME_INFO | `TEXT_CAROUSEL_GAME_INFO` | UI scale | 26 | "carry the stats size over to others" (the Brick's 33 entries) |
| 2026-10-07 | Smart Pro S | Home | Pinned games: per row | `HOME_WIDE_PIN_COLS` (`HomeLayoutOpts.wide_pin_cols`) | 4 | 3 | not text: "only 3 columns per row" |
| 2026-10-07 | Smart Pro S | Home | Pinned games: height | `HOME_PIN_K` (now also on wide screens) | 1× | 0.8× — the Continue card takes the height the pins give up | "reduce the height a bit, so we can have a taller Continue card" |
| 2026-10-07 | all (wide) | Home, 3+ pinned games | Tool squares | `home_layout.c` (Small scale) | fixed size (3 of them the old top's height) | (top height − 2 gaps) / 3: 3 a column filling the top section | not text: "fit exactly 3 items per column when we have 3 or more pinned games". Same as before at pin_k 1 |
| 2026-10-07 | all | Home | Pinned games: caption padding | `composeGame` (home.c) | 20 in, 16 up (its own) | the Continue card's: `CONT_INSET` from the sides, the time's and name's baselines as far up (`HomeLayout_captionBaselines`) | not text: "the Continue card's padding is bigger than the pinned games'; give the pins the same" |
| 2026-10-07 | all | Settings (options pages), Wi-Fi, Bluetooth | Selected row's pills | `GFX_blitPillColor` (was `GFX_blitRectColor(ASSET_BUTTON…)`) | rounded corners of the button art (radius 22 on a 51 px row at 2.25: flat ends) | true capsules: half-circle ends at the row's height | not text: "the options pills for the selected item was not perfectly rounded" |
| 2026-10-07 | Smart Pro S | Tools, Grid layout | Tile width | `GRID_TOOLS_W_K` | 1× | 1.5× (as the Brick and Brick Pro) | "increase the tools grid layout item to 1.5x" |

Untuned devices keep their current sizes in each entry (what the UI scale gives
them at Home's scale):

| Entry | Brick Pro | Smart Pro |
|---|---|---|
| `TEXT_HOME_STATS` | 28 (tuned) | 26 (tuned) |
| `TEXT_HOME_CONT_TITLE` | 28 (tuned) | 22 |
| `TEXT_HOME_CONT_INFO` | 28 (tuned) | 16 |
| `TEXT_HOME_PIN_NAME` | 28 (tuned) | 17 |
| `TEXT_HOME_PIN_INFO` | 28 (tuned) | 14 |
| `HOME_PIN_K` | 1× (2× tried) | 1× |
| `HOME_TOOL_ROWS` | 5 (tuned) | 4 |
| `TEXT_MENU_LIST` | 40 | 36 |
| `MENU_LIST_PITCH` | 75 | 68 |
| `MENU_LIST_ROWS` | 0 | 0 |
| `TEXT_LIST_INFO` | 28 (tuned) | 27 |
| `TEXT_GRID_TOOL` | 28 (tuned) | 0 |
| `GRID_TOOLS_W_K` | 1.5× (as the Brick) | 1.5× (tuned) |
| `TEXT_CAROUSEL_COUNT` | 28 (tuned) | 0 |
| `TEXT_CAROUSEL_COLL_NAME` | 41 (tuned) | 0 |
| `CAROUSEL_COLL_W_K` | 1.5× (as the Brick) | 1× |
| `TEXT_CAROUSEL_TOOL` | 31 (tuned) | 0 |
| `CAROUSEL_TOOL_W_K` | 1.5× (as the Brick) | 1× |
| `CAROUSEL_ITEM_GAP_K` | 2× (as the Brick) | 1× |
| `TEXT_CAROUSEL_GAME_NAME` | 34 (tuned) | 0 |
| `TEXT_CAROUSEL_GAME_INFO` | 28 (tuned) | 0 |
| `CAROUSEL_GAME_SEL_K` | 1.4× (as the Brick) | 1× |
| `CAROUSEL_CAPTION_INFO_LINES` | 2 (set with the Brick) | 1 |
