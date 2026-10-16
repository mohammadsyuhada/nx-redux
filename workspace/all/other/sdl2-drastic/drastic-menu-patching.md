# Drastic Menu Patching Guide

## Background

DraStic (NDS.pak) on both tg5040 and tg5050 uses a custom SDL2 build with hook
code from `0006-add-hook-for-drastic.patch` (trngaje/knulli_distribution), plus
our per-platform `fix-tg5040.patch` / `fix-tg5050.patch`. This compiles menu
rendering and input handling directly into SDL2 (`drastic_video.c`).

The drastic binary is vanilla `r2.5.2.0`. The hook code intercepts its
rendering and overlays a custom menu system.

## Architecture

```
drastic (vanilla r2.5.2.0)
  └── loads libSDL2-2.0.so.0 (our custom build)
        ├── Hook patch (drastic_video.c) — rendering, input, menus
        ├── DRM rendering (ADVDRASTIC_DRM) — GBM/EGL, tg5050 only
        ├── fbdev rendering — /dev/fb0 + EGL, tg5040 (no ADVDRASTIC_DRM)
        └── Dummy driver fix — reports real display resolution (DRM / fb ioctl)
```

## Current Menu Items (after NX Redux patches)

The hook code builds the menu in `sdl_create_menu_main` via `puVar10[]` array.
Items are reordered so Load/Save state appear first. "Change Steward Options"
is removed entirely. "Load new game" is hidden at render time.

| Index | Text                       | Type     | Notes                        |
|-------|----------------------------|----------|------------------------------|
| 0     | Load state   0             | MenuItem | Load savestate (top)         |
| 1     | Save state   0             | MenuItem | Save savestate               |
| 2     | Change Options             | MenuItem | Display/audio settings       |
| 3     | Configure Controls         | MenuItem | Button mapping               |
| 4     | Configure Firmware         | MenuItem | NDS firmware (username etc)  |
| 5     | Configure Cheats           | MenuItem | Cheat codes                  |
| 6     | Load new game              | MenuItem | ROM browser (hidden)         |
| 7     | Restart game               | MenuItem | Restart current ROM          |
| 8     | Return to game             | MenuItem | Resume playing               |
| 9     | Exit DraStic               | MenuItem | Quit emulator                |

## How Menu Rendering Works

### Menu capture (`get_current_menu_layer`)

The hook intercepts `sdl_print_string` calls from drastic. Each string is
stored in `drastic_menu.item[cc]` with its x, y, text, foreground/background
colors. The function `get_current_menu_layer()` identifies which menu layer
is active by matching known text patterns:

```c
// Pattern matching in get_current_menu_layer():
P0 = "Exit DraStic"      → NDS_DRASTIC_MENU_MAIN
P1 = "Screen orientation" → NDS_DRASTIC_MENU_OPTION (Change Options submenu)
P2 = "    Up"             → NDS_DRASTIC_MENU_CONTROLS
P3 = "    Up"             → NDS_DRASTIC_MENU_CONTROLS (alternate x position)
P4 = "Favorite color"     → NDS_DRASTIC_MENU_FIRMWARE
P6 = (ROM path pattern)   → NDS_DRASTIC_MENU_ROM
```

### Menu drawing (`draw_drastic_menu_main`)

For each captured menu item, the hook decides how to render it:

- **y == 201** (version): Rendered above the menu items
  ```c
  sprintf(buf, "DraStic %s", p->msg);
  x = g_advdrastic.iDisplay_width * 90 / 640;
  y = h - w * 2;
  ```

- **x == 180** (menu items): Rendered in a vertical list with cursor highlight
  ```c
  y = h + (menu_line_cnt * LINE_H);
  ```

- **x == 544** (savestate preview): Rendered as a screenshot thumbnail

### Menu scrolling

When cursor is at top (cursor <= 6), items start from index 1 (skipping
version line). When at bottom, the view scrolls. 13 items visible at once
(s0 to s1, range of 14).

## How to Hide Menu Items

Hidden items are listed in `is_hidden_menu_item()` in `drastic_video.c`
(currently only "Load new game"); `draw_drastic_menu_main()` skips them by
their text content:

```c
static int is_hidden_menu_item(const char *msg)
{
    if (!memcmp(msg, "Load new game", 13)) return 1;
    if (!memcmp(msg, "Configure Firmware", 18)) return 1;  // add more like this
    return 0;
}

// Inside draw_drastic_menu_main():
else if (p->x == 180) {
    if (is_hidden_menu_item(p->msg)) continue;
    ...
}
```

This filters items at render time. The original drastic menu logic still
works (cursor navigation, selection), but hidden items are simply not drawn.

**Important**: Items are identified by text prefix, not index. The indices
can shift between drastic versions. Text matching is more robust.

## How to Rename Menu Items

Same approach — match the original text and replace:

```c
if (!memcmp(p->msg, "Change Options", 14)) {
    strcpy(buf, "Display Settings");
} else {
    strcpy(buf, to_lang(drastic_menu.item[cc].msg));
}
```

## Build Pipeline

Our changes live as patches on top of the pinned SDL_drastic source
(`eb2e00f`, knulli2.30.8):

```
workspace/all/other/sdl2-drastic/
├── 0006-add-hook-for-drastic.patch   # Shared hook patch (both platforms)
├── fix-tg5040.patch                  # Our tg5040 changes (menu, input, fbdev, ...)
├── fix-tg5050.patch                  # Our tg5050 changes (menu, input, DRM, ...)
├── build-sdl2-drastic-tg5040.sh      # Build recipe (no ADVDRASTIC_DRM)
├── build-sdl2-drastic-tg5050.sh      # Build recipe (ADVDRASTIC_DRM)
├── gen-patch.sh                      # Regenerates fix-<platform>.patch
└── Makefile                          # make build / deploy PLATFORM=<platform>

workspace/<platform>/other/sdl2-drastic/   # per-platform work dir [gitignored]
├── SDL_drastic/                       # Source (eb2e00f + 0006 + fix patch)
│   └── src/video/drastic_video.c      # Hook code with menu system
├── extra-deps/                        # Build-time headers/stubs
└── output/                            # Build output
```

Release builds do not use this work dir: `workspace/all/prebuilts/sdl2-drastic.sh`
reclones the pinned source, applies `0006` + `fix-<platform>.patch`, and runs the
same build recipe (see its header for the docker command).

### Editing and rebuilding

```sh
# Clone + patch the source (from workspace/<platform>, inside the toolchain)
make other/sdl2-drastic/SDL_drastic

# Edit workspace/<platform>/other/sdl2-drastic/SDL_drastic/src/video/drastic_video.c, then
cd workspace/all/other/sdl2-drastic
make build PLATFORM=tg5050        # or tg5040; output in workspace/<platform>/other/sdl2-drastic/output/
./gen-patch.sh tg5050             # write the edits back into fix-tg5050.patch
```

### Deploy to device

```sh
# Copies the build into skeleton/SYSTEM/<platform>/paks/Emus/NDS.pak/libs/
make deploy PLATFORM=tg5050

# Push to device via ADB
adb push skeleton/SYSTEM/tg5050/paks/Emus/NDS.pak/libs/libSDL2-2.0.so.0 \
  /mnt/SDCARD/.system/paks/Emus/NDS.pak/libs/libSDL2-2.0.so.0
```

## tg5040 vs tg5050

| Aspect | tg5040 | tg5050 |
|--------|--------|--------|
| Display | 1024x768, fbdev EGL | 1280x720, GBM/DRM EGL |
| SDL2 | Hook patch compiled into SDL2 | Hook patch compiled into SDL2 |
| Fix patch | fix-tg5040.patch | fix-tg5050.patch |
| CFLAGS | -DDEVICE_TRIMUI | -DDEVICE_TRIMUI -DADVDRASTIC_DRM |
| SDL_VIDEODRIVER | dummy | dummy |

## Key Source References

- Hook patch: `https://raw.githubusercontent.com/trngaje/knulli_distribution/knulli-main/package/batocera/emulators/advanced_drastic/sdl2_drastic/0006-add-hook-for-drastic.patch`
- SDL_drastic source: `https://github.com/trngaje/SDL_drastic` (branch: knulli2.30.8)
- Build makefile (knulli): `sdl2_drastic.mk` in same directory as patch
- libadvdrastic.so exports: `drastic_VideoInit`, `GFX_Init`, `fb_init`, `AD_create_menu_*`, `AD_draw_menu_*`, `detour_init`, `init_IMG_egl`

## Completed

- [x] Hide "Load new game" menu item (direct edit in drastic_video.c)
- [x] Restore original version header ("DraStic <version>" above the menu items)
- [x] Remove the left-side "Advanced Drastic" title
- [x] Implement `sdl_get_gui_input` hook — fixes B button not working in menus
- [x] Add NULL check + fallback for `TTF_OpenFont` (prevents crash on bad font)
- [x] Move "Load state" and "Save state" to top of menu (reordered in `sdl_create_menu_main`)
- [x] Center savestate screenshot (was right-aligned, now centered horizontally)

## TODO

- [ ] Hide additional items: "Configure Firmware", "Restart game"
- [ ] Custom font — font1.ttf (8MB) caused black screen, needs investigation
- [ ] Menu colors — configurable via `resources/settings.json` (menu_c0, menu_c1, menu_c2)
