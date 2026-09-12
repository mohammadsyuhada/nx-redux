# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

Work that is **planned but not yet built** does not belong here — it lives in `DEV_TODO.md`.
Move an entry from there to here once it compiles and needs hardware time.

---

## Game art style + type (Appearance) / Artwork Manager stores all variants (built 2026-09-12)

Settings -> Appearance gained **Game art style** (`artStyle=0|1`, Thumbnail /
Background) and **Game art type** (`artType=0|1|2`, Mix / Screenshot / Box
art). Background's geometry is FIXED (Game art width sizes the thumbnail
style only, including its title-column clamp): the fade runs from 40% in
from the right edge at the top row to 60% in at the bottom on a 4:3 panel,
20%/40% on a wide one (`ART_BG_WIDE_ASPECT` 1.5, i.e. the 16:9 Smart Pro S),
the art is
scaled to FIT the screen height (a source taller than wide — DS screenshot
variants are 320x480 — is instead scaled to the DRAWN BOX WIDTH and cropped
to the top band that fills the screen height, no fixed fraction:
`band = H * art_w / box_w`, which lands at 233/480 source rows on the Brick
(97% of the top screen) and 240/480 on the Smart Pro S (100%, clamped at the
screen boundary so a seam of the second screen never shows)) and always pushed right by
`ART_BG_OVERFLOW` 0.30 of its width; where that leaves the image starting
later than the feathered boundary, the ramp starts at the image's own left
edge instead (`ramp_start = max(draw_left, xb - feather)`), which is what
keeps any aspect ratio free of a hard vertical cut without clamping the
shift or cropping. The ramp is smootherstep SQUARED (the dark end then
holds near zero far longer, so a bright screenshot blends instead of
announcing its edge) over the whole span to the right edge, with a 0.25
feather; titles are capped at `ART_BG_TEXT_WIDTH` 0.85 of the screen.
Composited in software on the thumb-loader thread
(`nextui/artbg.c`, tunables at the top) and painted onto `LAYER_BACKGROUND`
with black + folder bg, because that layer composites without alpha (only
layers 2-5 blend) and it must sit under the status bar / pills. The
background style always resolves to the screenshot
(`CFG_getEffectiveArtType`) and never falls back: `ROM_displayArtPath`'s
`fallback_to_mix` is false there, so a game with no screenshot shows an
empty background. The thumbnail style keeps the fallback to the root Mix
file. The Artwork Manager writes all
three PNGs per fetch (`.media/<g>.png`, `.media/screenshot/<g>.png`,
`.media/boxart/<g>.png`) from one download set — GUI queue and headless
`--fetch` alike — and its Settings page keeps **Reset artwork** (every png
under `Roms/*/.media` incl. both variant subfolders, which are rmdir'd when
empty; `bg.png`/`bglist.png` kept; confirm modal; refused while the queue
runs); its page-level "Log in for higher rate limits" status line was
dropped. Docs carry a "v1.9.0 or older: reset and re-fetch" note on both the
Appearance and Artwork Manager pages, and the Artwork Manager screenshot was
retaken (`docs/assets/screenshots/artwork-settings.png`, Brick capture).

Device-verified headlessly on Brick (fb composite captures) and Smart Pro S
(DRM shots), including the empty-background case (a game with no screenshot
variant draws no art) and the fixed geometry on both aspect ratios (4:3
Brick and 16:9 Smart Pro S, no hard image edge inside the fade) and art
tracking the selection row by row: one
headless fetch writes all three files on both devices;
Background shows the screenshot variant edge-to-edge (no Mix inset strip);
Thumbnail + Box art shows the box art; a game with only old Mix art falls
back correctly; the Appearance row cycles; Reset took 192 mix + 2 variants
-> 0 with the 30 folder backgrounds and both variant dirs gone, then the
Brick backup was restored. Host tests: `scripts/tests/test-art-style.sh`
(config + path resolution + compositor), `test-scraper-variants.sh`.
Brick + Smart Pro S LEFT in Background style, type Mix. The Brick Pro got
the full `make deploy DEVICE=brickpro` zip on 2026-09-12 (it was still on the
2026-09-10 card, and loose elfs crash-looped it because libmsettings had
gained the rumble symbols); it stays on its own Thumbnail setting and has no
screenshot variants fetched yet.

- [ ] Hands-on look at the fade on both panels. Tunables in
      `nextui/artbg.c`: `ART_BG_TOP_FROM_RIGHT` 0.40 (0.20 wide) /
      `ART_BG_BOTTOM_FROM_RIGHT` 0.60 (0.40 wide) / `ART_BG_FEATHER` 0.25 /
      `ART_BG_FADE_SPAN` 1.00 / `ART_BG_MAX_ALPHA` 1.0 / `ART_BG_OVERFLOW`
      0.30 / `ART_BG_EDGE_FADE` 0.06.
- [x] Fade went vertical below ~1/3 height (user spotted it): `ramp_start`
      was clamped to the image's left edge, and with the 0.25 feather that
      clamp bound on every lower row, so they all shared one profile. The
      ramp is geometric again and the image's own edge is hidden by a short
      local fade (`ART_BG_EDGE_FADE`) instead. Device-measured onset now
      936px at the top row to 768px at the bottom on the Smart Pro S.
- [ ] The tall-source rule keys on aspect < 1.0, so a portrait arcade shot
      (e.g. 3:4) is also top-cropped. Check that reads acceptably, or narrow
      the rule to the DS 2:3 shape.
- [ ] Wide panels now use a 666px strip (was 922px at 40/60): every game's
      art area on the Smart Pro S is smaller, not just the DS ones. Check a
      normal 4:3 screenshot still reads well there.
- [ ] On 16:9 the shifted image starts right of the nominal bottom boundary,
      so the bottom rows fade over a shorter run than the top rows. Check it
      still reads as one diagonal on the Smart Pro S panel.
- [ ] Background titles are capped at 85% of the screen width
      (`ART_BG_TEXT_WIDTH` in `nextui/artbg.h`); check long names read well
      over the image on both panels.
- [ ] Selection-change cost in Background style: one full-screen layer
      upload per change (black + folder bg + art) instead of a small thumb;
      glide deferral still applies. Check it feels fine on the Brick.
- [ ] Artwork Manager Library -> Queue All writes all three variants (only
      the headless single-ROM path is exercised on hardware).
- [ ] Search screen in Background style / with a non-Mix type (same loader
      path, not captured).
- [ ] Disk cost: three PNGs per game instead of one (~2-3x). No pruning
      option exists; Reset artwork is the only bulk cleanup.

## Desktop: AppImage no longer bundles the host GL/driver stack (issue #86; built 2026-09-04)

v1.9.0's AppImage died before opening a window on every Mesa >= 25 host
(Ubuntu 24.04.4 HWE, Mint 22, 25.10): the ldd sweep bundled Ubuntu 22.04's
libstdc++/libGL/libGLX/libGLdispatch, AppRun puts usr/lib first on
LD_LIBRARY_PATH, and the host driver's libLLVM.so.20 then failed with
`GLIBCXX_3.4.32 not found` -> no GLX visual -> NULL window/renderer used
anyway -> segfault. Fix: `scripts/desktop/appimage-bundle-libs.sh` (vendored
`appimage-excludelist` + driver-adjacent families; freetype/harfbuzz kept),
and `PLAT_initVideo` now logs the SDL error and falls back to a plain window
+ software renderer instead of crashing.

Verified in amd64 containers under Xvfb/llvmpipe only (Ubuntu 24.04.4 +
25.10 with Mesa 25.2.8, and the 22.04 floor): trimmed bundle opens on the
`opengl` driver with libGL/libLLVM/libstdc++ all host-side, the old bundle +
new binary comes up on the software fallback. Not yet seen on a real GPU or
in a CI-built artifact (no local cores tree; the release workflow rebuilds
from scratch).

- [ ] Next release's AppImage on a real Ubuntu 24.04 / Mint 22 host with a
      GPU driver: `nextui.txt` shows `Current render driver: opengl`, no
      `[ERROR]` lines, no env vars needed (ask the #86 reporter to confirm).
- [ ] Same artifact on the oldest supported host (glibc 2.35 / Ubuntu 22.04):
      still opens (only host-provided libs were removed; a host missing one
      now fails with a clear "cannot open shared object file" line).
- [ ] `scripts/desktop/test-appimage-e2e.sh` in the 22.04 compose image still
      passes end to end (menu -> ROM launch) against the rebuilt AppImage.
- [ ] Optional: force the fallback (`SDL_VIDEO_X11_VISUALID= ` no longer
      needed; e.g. run on a GL-less VM) and confirm the menu + a non-shader
      game render on `software`, with `SDL_GL_CreateContext failed ...
      (shaders unavailable)` logged rather than a crash.

## Desktop: Tools paks bundled into packages; Wifi_ensureConnected behavior change (built 2026-09-01)

Task 11 of the desktop-tools-and-networking SDD: all 7 Tools paks (Settings,
Emulator Settings, RetroAchievements, Artwork Manager, Device Sync, Xtras,
Game Tracker) now ship inside both the macOS `.app` and the Linux AppImage,
pak-local (`paks/Tools/<Name>.pak/<binary>.elf`, matching device layout) with
`gametimectl.elf` also at `$SYS/bin` (nextui.c/launcher.c invoke it bare via
PATH at ROM start/stop — it's a stateless CLI, not a daemon, despite the
name). `PLAT_getNetworkStatus` on desktop now delegates to the already-live
`PLAT_wifiConnected()` reachability probe instead of hardcoding offline —
this feeds both the shared menu-bar wifi icon (`pwr.is_online`, `api.c`
`PWR_updateNetworkStatus`) and Device Sync's `STATE_NO_WIFI` gate (`sync.c`).
Verified live (macOS process/dylib-load + a Linux Xvfb/xdotool E2E,
`scripts/desktop/test-appimage-tools-e2e.sh`): Tools menu lists all 7 paks,
Settings shows only the desktop-retained sections, a pak (Artwork Manager)
opens cleanly, and the menu-bar wifi icon tracks real reachability. Desktop
build/packaging itself needs no further hardware verification — the items
below are about a *separate*, already-shipped device-side behavior change
this task's investigation surfaced (`Wifi_ensureConnected`, `wifi.c`) that
had no existing DEV_CHECKLIST coverage, plus desktop tools whose *content*
(network calls, real accounts) can't be exercised on desktop hardware.

**`Wifi_ensureConnected` — never auto-enables/auto-connects (both devices):**
- [ ] Wifi OFF, open Music Player → Podcasts (or Radio) → attempting to load
      a feed/stream shows "WiFi is off. Enable it in Settings." and wifi
      stays OFF (check Settings — it must not have flipped itself on).
- [ ] Wifi OFF, open Media Player → IPTV → same message, same
      does-not-auto-enable check.
- [ ] Wifi ON but not yet associated (no AP in range / wrong password):
      same screens instead show "WiFi is not connected. Connect in
      Settings." (`wifi.c` `Wifi_ensureConnected` picks the message off
      `PLAT_wifiEnabled()` — confirm it doesn't say "WiFi is off" once the
      radio is actually on).
- [ ] Wifi ON + connected: podcast/radio streaming and IPTV both work as
      before (no regression from the message-only change).

**Desktop tools needing real hardware/network verification** (all built +
packaged; none of this is testable from a desktop dev machine):
- [ ] Device Sync: an actual sync target (a real device or PC on the LAN)
      — Device Sync.pak's transfer paths are unexercised beyond the
      STATE_NO_WIFI gate fix above.
- [ ] RetroAchievements: real login + a live achievement unlock through
      ratools.elf on desktop (network calls only smoke-tested for
      reachability, not for actual RA account auth/session flow).
- [ ] Xtras: catalog installs will fail on desktop today (packages are
      device-arch binaries) — expected, not a bug; note this if anyone
      files it. Revisit if/when Xtras gains a desktop-arch catalog.

---

## Desktop: external game-controller support (built 2026-09-01)

Desktop build gained `SDL_GameController` support (macOS `.app` / Linux AppImage),
behind a `HAS_GAMECONTROLLER` macro defined only in `workspace/desktop/platform/platform.h`
— device builds compile the path out entirely (verified: tg5040 + tg5050 build green).
Keyboard stays active simultaneously. Face buttons map by physical position (Nintendo
layout: right face = A/confirm, bottom = B/back). Left stick = analog passthrough to
cores (`RETRO_DEVICE_ANALOG`) **and** digital d-pad (deadzone) for menus / non-analog
cores; right stick = analog only. Triggers (L2/R2) are digital past a threshold. Hot-plug
handled via `SDL_CONTROLLERDEVICEADDED/REMOVED`. No controller was available at build
time, so all controller behavior below is UNVERIFIED on real hardware.

- [ ] Plug an Xbox-style pad in **before** launch → launcher navigates with d-pad + left
      stick; right face button confirms, bottom face backs out (Nintendo-position mapping).
- [ ] Hot-plug: launch with no pad, connect one → it starts working without relaunch;
      disconnect → app stays alive, keyboard still works.
- [ ] Disconnect while holding a direction/button or with a stick off-center →
      no stuck input afterwards (CONTROLLERDEVICEREMOVED zeroes analog axes +
      PAD_reset()).
- [ ] In-game: face/d-pad/shoulders drive the emulated pad; keyboard still works at the
      same time (both input sources live).
- [ ] Analog passthrough: a core that reads analog (e.g. an N64/PSX core if present on
      desktop, else any `RETRO_DEVICE_ANALOG` core) responds to the right stick; left stick
      also moves the character AND navigates menus as a d-pad.
- [ ] Try a second controller type if available (PS4/5, Switch Pro, 8BitDo) — SDL's
      built-in DB should map it with no per-device config; note any pad that isn't recognized
      (would need a bundled `gamecontrollerdb.txt`, deferred).

---

## Boot: failed MinUI.zip extraction must not brick the boot loop (built 2026-08-01)

Found live on Smart Pro S (fresh install, 2026-08-01): a truncated MinUI.zip
(card pulled before the 230 MB copy flushed) made `.tmp_update/<plat>.sh`
extract nothing, then `rm -f MinUI.zip` unconditionally — every later boot had
no zip, no `.system`, no splash, and fell through to `poweroff`. Looks like a
dead device. Fixed in both `workspace/{tg5040,tg5050}/install/boot.sh`: the
zip is consumed only when unzip succeeds; on failure a show2 error line is
displayed for 10 s and the zip is kept so the next boot retries. The pakz
loop got the same success-gated consume — a corrupt pakz is renamed
`<name>.failed` (kept for diagnosis, but not re-matched by the `*.pakz` glob,
so no per-boot retry nag) and boot continues normally.

- [ ] Happy path: fresh install extracts and launches normally (both devices).
- [ ] Corrupt-zip path: truncate a MinUI.zip on card (`head -c 10M`), boot →
      "Install failed" splash shows ~10 s, MinUI.zip still on card, device
      powers off; replacing the zip and rebooting installs cleanly.
- [ ] Corrupt-pakz path: truncate a pakz on card, boot → "Package install
      failed" splash ~5 s, file renamed `.failed`, system boots normally and
      the next boot does NOT re-attempt it.

---

## Upstream-port + fix round (built 2026-07-27)

**Status:** ten DEV_TODO items implemented 2026-07-27, committed as `1ccc1030`. All
changed elfs + the rebuilt GLideN64 `.so` + N64 launch.sh are pushed to both cards
(Brick and Smart Pro S, md5-verified 2026-07-27).

Full deploy: `make all`, flash zip. Quick iterate: push the single rebuilt `.elf` (reboot required
for nextui/minarch pushes — see Gotchas at the bottom of this file).

### On-device verification

- [ ] **SRAM read unification** (`ma_saves.c`, upstream #667) — save in-game with
      Save Format = SRM (compressed), switch back to the default (uncompressed), relaunch:
      the in-game save must be intact, and after the next in-game save the `.srm` should be
      raw (`head -c8` no longer `#RZIPv1#`). Regression: raw `.srm` still loads, and a
      RetroArch-imported compressed `.srm` loads under the default setting.
- [ ] **Rewind re-init fix** (upstream #728 + early-out) — enable rewind, play: rewind
      works; changing a rewind option mid-game still takes effect (buffer size change →
      re-init happens); in-game "Restore Defaults" no longer hitches for seconds with a
      big rewind buffer; game launch with rewind enabled allocates once (single
      "Rewind:" init in the log, if logging shows it).

### Follow-ups discovered while implementing

- The `keepAwakeUSB` config key is camelCase, matching its immediate neighbours
  (`disableSleep`, `sshOnBoot`) rather than the older lowercase style the DEV_TODO entry
  suggested — deliberate.
- CFG setters were NOT given per-setter early-returns: `CFG_sync()` now compares content
  before writing, which subsumes the I/O benefit (a redundant set costs a read+compare,
  never a write).
- Core-requested SHUTDOWN (env cmd 7) deliberately does NOT trigger the slot-9 autosave —
  it fires mid-`retro_run` where a state save is unsafe, and the quitting core (Doom quit
  menu / failed init) rarely has a moment worth resuming. FBNeo's error screen verified
  on both devices 2026-09-10 (needed the lazy input poll in `ma_input.c`, since that
  screen never calls `input_poll_callback`); the PRBOOM quit check and the resampler
  PAL soak were closed without hardware verification (user decision, 2026-09-10).
