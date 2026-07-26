# Drastic Menu Patching Guide

## Background

Advanced Drastic on tg5050 uses a custom SDL2 build with hook code from
`0006-add-hook-for-drastic.patch` (trngaje/knulli_distribution). This patch
compiles menu rendering and input handling directly into SDL2 (`drastic_video.c`).

The drastic binary is vanilla `r2.5.2.0`. The hook code intercepts its
rendering and overlays a custom menu system.

## Architecture

```
drastic (vanilla r2.5.2.0)
  └── loads libSDL2-2.0.so.0 (our custom build)
        ├── Hook patch (drastic_video.c) — rendering, input, menus
        ├── DRM rendering (ADVDRASTIC_DRM) — GBM/EGL for tg5050
        ├── Dummy driver fix — reports real display resolution from DRM
        └── libadvdrastic.so — NOT loaded (conflicts with compiled-in hooks)
```

On tg5040, a different SDL2 build loads `libadvdrastic.so` via dlopen, which
provides the "Advanced Drastic (1.1-tr-251109)" version and extra menu features.
That approach doesn't work on tg5050 because libadvdrastic.so uses fbdev EGL
which tg5050's Mali GPU doesn't support (requires GBM EGL).

## Current Menu Items (after NextUI patches)

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

- **y == 201** (version): Rendered top-right with custom formatting
  ```c
  sprintf(buf, "Advanced NDS");  // was: "NDS %s", &p->msg[8]
  x = display_width - get_font_width(buf) - 10;
  y = display_height * 10 / 480;
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

To hide specific items, modify `draw_drastic_menu_main()` in `drastic_video.c`.
The simplest approach is to skip items by their text content:

```python
# In fix-menu.py, add skip logic after the "memset(buf, 0, sizeof(buf));" line:

# Items to hide (by text prefix):
HIDDEN_ITEMS = [
    "Change Steward Options",
    "Configure Firmware",
    "Load new game",
    "Restart game",
]
```

Implementation approach — add a skip check in the menu item loop:

```c
// Inside draw_drastic_menu_main(), after: p = &drastic_menu.item[cc];
// Add:
if (p->x == 180) {
    // Skip hidden items
    if (!memcmp(p->msg, "Change Steward", 14)) continue;
    if (!memcmp(p->msg, "Configure Firmware", 18)) continue;
    // ... etc
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

All patches are Python scripts applied during the Docker build:

```
workspace/tg5050/other/sdl2-drastic/
├── build-sdl2-drastic.sh          # Main build script
├── fix-drm-init.py                # Step 3b: DRM connector auto-detect + GBM resolution
├── fix-dummy-resolution.py        # Step 3c: Dummy driver reads DRM resolution
├── fix-menu.py                    # Step 3d: Menu customization
├── fix-perf.py                    # Step 3e: DRM rendering perf (double gbm lock fix)
├── SDL_drastic/                   # Source (knulli2.30.8 + patches) [gitignored]
│   └── src/video/drastic_video.c  # Hook code with menu system
├── extra-deps/                    # Build-time headers/stubs [gitignored]
└── output/                        # Build output [gitignored]
```

Sentinel files in SDL_drastic/ track which patches have been applied:
- `.patched` — 0006 hook patch
- `.drm_fixed` — DRM init fix
- `.dummy_fixed` — Dummy driver resolution fix
- `.menu_fixed` — Menu customization
- `.perf_fixed` — DRM performance fix

To force re-patching: `rm SDL_drastic/.menu_fixed` then rebuild.

### Full rebuild (from scratch)

```sh
cd workspace/tg5050/other/sdl2-drastic
docker run --rm -v .:/root/workspace \
  ghcr.io/loveretro/tg5050-toolchain:latest \
  bash /root/workspace/build-sdl2-drastic.sh
```

### Incremental rebuild (after editing drastic_video.c directly)

When editing `SDL_drastic/src/video/drastic_video.c` directly (bypassing the
Python fix scripts), use an incremental `make` to avoid re-running configure:

```sh
cd workspace/tg5050/other/sdl2-drastic
docker run --rm -v .:/root/workspace \
  ghcr.io/loveretro/tg5050-toolchain:latest \
  bash -c '
# Fix broken .la files in sysroot (needed every fresh container run)
BROKEN_PREFIX="/home2/trimuidev/tg5050/tina_1.0.0/out/a523/gb1/buildroot/buildroot/host/bin/../aarch64-buildroot-linux-gnu/sysroot"
for la in ${SYSROOT}/usr/lib/*.la; do
    if grep -q "$BROKEN_PREFIX" "$la" 2>/dev/null; then
        sed -i "s|$BROKEN_PREFIX|$SYSROOT|g" "$la"
    fi
done
cd /root/workspace/SDL_drastic
make -j$(nproc) EXTRA_LDFLAGS="-L/root/workspace/extra-deps/lib -L${SYSROOT}/usr/lib -lSDL2_image -lSDL2_ttf -ljson-c -lpthread -lEGL -lGLESv2 -ldrm -lgbm"
cp build/.libs/libSDL2-2.0.so.0 /root/workspace/output/
'
```

### Deploy to device

```sh
# Copy to skeleton
cp workspace/tg5050/other/sdl2-drastic/output/libSDL2-2.0.so.0 \
   skeleton/EXTRAS/Emus/shared/Drastic/devices/tg5050/libs/libSDL2-2.0.so.0

# Push to device via ADB
adb push skeleton/EXTRAS/Emus/shared/Drastic/devices/tg5050/libs/libSDL2-2.0.so.0 \
  /mnt/SDCARD/Emus/shared/Drastic/devices/tg5050/libs/libSDL2-2.0.so.0
```

Output: `workspace/output/libSDL2-2.0.so.0`

## tg5040 vs tg5050

| Aspect | tg5040 | tg5050 |
|--------|--------|--------|
| Display | 1024x768, fbdev EGL | 1280x720, GBM/DRM EGL |
| SDL2 | Loads libadvdrastic.so via dlopen | Hook patch compiled into SDL2 |
| Menu | From libadvdrastic.so (advanced) | From hook patch (customizable) |
| Version shown | Advanced Drastic (1.1-tr-251109) | DraStic v1.9 + original version header |
| CFLAGS | -DDEVICE_TRIMUI | -DDEVICE_TRIMUI -DADVDRASTIC_DRM |
| SDL_VIDEODRIVER | (default/mali) | dummy |

To unify the menu across both devices, build tg5040's SDL2 with the same
hook patch (without ADVDRASTIC_DRM flag) and the same fix-menu.py patches.
This would replace libadvdrastic.so integration with the compiled-in hooks.

## Key Source References

- Hook patch: `https://raw.githubusercontent.com/trngaje/knulli_distribution/knulli-main/package/batocera/emulators/advanced_drastic/sdl2_drastic/0006-add-hook-for-drastic.patch`
- SDL_drastic source: `https://github.com/trngaje/SDL_drastic` (branch: knulli2.30.8)
- Build makefile (knulli): `sdl2_drastic.mk` in same directory as patch
- libadvdrastic.so exports: `drastic_VideoInit`, `GFX_Init`, `fb_init`, `AD_create_menu_*`, `AD_draw_menu_*`, `detour_init`, `init_IMG_egl`

## Completed

- [x] Hide "Load new game" menu item (direct edit in drastic_video.c)
- [x] Restore original version header (shows drastic's `p->msg` instead of "Advanced NDS")
- [x] Change left-side title from "Advanced Drastic" to "DraStic v1.9"
- [x] Implement `sdl_get_gui_input` hook — fixes B button not working in menus
- [x] Add NULL check + fallback for `TTF_OpenFont` (prevents crash on bad font)
- [x] Move "Load state" and "Save state" to top of menu (reordered in `sdl_create_menu_main`)
- [x] Center savestate screenshot (was right-aligned, now centered horizontally)

## TODO

- [ ] Hide additional items: "Configure Firmware", "Restart game"
- [ ] Custom font — font1.ttf (8MB) caused black screen, needs investigation
- [ ] Menu colors — configurable via `resources/settings.json` (menu_c0, menu_c1, menu_c2)
- [ ] Optionally build same SDL2 for tg5040 (without ADVDRASTIC_DRM)
