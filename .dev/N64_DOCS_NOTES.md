# N64 on minarch: notes for the docs site

User-visible changes from moving Nintendo 64 off the standalone mupen64plus onto the mupen64plus-next libretro
core in minarch (issue #149). The docs site lives in `~/Work/Personal/nx-redux-docs`; update the Nintendo 64
page and the README's "bundled standalone emulators" line from these.

## What players get

- **RetroAchievements** for N64 (the reason for the move): log in under Settings as for every other system.
- **The standard in-game menu** (MENU): save/load states with screenshots, game switcher, shaders/scaling,
  Emulator Options, quit. The standalone's own overlay menu and its settings are gone.
- Rewind is off by default (same as the other heavy systems).

## Buttons (same as before the move)

- N64 A = A, N64 B = B, C-Down = Y, C-Left = X, all four C buttons on the right stick, Z = L2, L = L1,
  R = R1, Start = Start. They follow the Nintendo/Xbox button-layout setting like every other system.
- Remap per game or for all N64 games in the in-game menu (MENU → Controls).

## Video renderers

- Two renderers are built in: **Rice** (light) and **GLideN64** (accurate).
- Defaults: **Rice** on the Brick, Brick Pro and Smart Pro; **GLideN64** on the Smart Pro S — the same defaults
  the standalone had.
- Switch per game or for all games: game list → context menu → **Emulator Options** → RDP Plugin.
- **Doom 64 needs GLideN64**: with Rice the picture is black (it always was, in the standalone too). On the
  Brick, GLideN64 is GPU-heavy: Doom 64 runs below full speed.

## Saves

- A game's battery saves (EEPROM, SRAM, FlashRAM) and Controller Pak data carry over **automatically the first
  time the game is launched** after the update. The old files stay where they were, untouched.
- If the game already has a save under the new system, that save is used and nothing is copied.
- **Save states do not carry over** (the standalone's states can't be read by the new core). Finish the session
  or make an in-game save before updating.
- New saves are in `Saves/N64/<game>.srm`, states in the usual minarch place.

## Hi-res texture packs (GLideN64 only)

- **Off by default.** Turn on per game (or for all N64 games) in Emulator Options → "Use High-Res textures"
  (needs the GLideN64 renderer; Rice ignores packs).
- Packs go where they always did: PNG packs in `Roms/Nintendo 64 (N64)/.hires_texture/<GAME INTERNAL NAME>/`,
  ready-made caches (`<GAME>_HIRESTEXTURES.hts`) in `Roms/Nintendo 64 (N64)/.cache/`. A cache is used when present,
  otherwise the PNG pack is converted into one on the first launch, with a "Processing hi-res textures (first time
  only)..." screen; that can take several minutes for a big pack.
- Textures are read from the cache as the game first uses them: expect short stutters when new scenery appears.
- Pack instructions written for desktop GLideN64 (e.g. MK64 Reloaded) map to N64.pak like this:
  "GLideN64 as video plugin" = RDP Plugin GLideN64 (default on the Smart Pro S; switch per game on the Brick-class
  devices); "Use texture pack" = Use High-Res textures; "file storage instead of memory cache" and "Fix black
  lines between 2D elements: For adjacent 2D elements" are already the defaults; resolution is 2x native.
- Memory: at most 200 MB of hi-res textures are kept loaded (1 GB devices). While an N64 game runs with packs on
  the card, a 512 MB swap file on the internal storage backs it up (created on the first such launch).
- Tested: a 3.6 GB uncompressed Mario Kart 64 cache on the Smart Pro S (HD textures, occasional stutter, ~640 MB
  used after 3 minutes, game stable).

## Netplay

- Unchanged for players: Y / "Launch with Netplay", up to 4 players, Wi-Fi or USB cable.
- Both devices must run this version (the old standalone and the new core don't talk to each other).
- Devices on different renderers (e.g. Brick on Rice, Smart Pro S on GLideN64) can play together: only inputs
  are exchanged, each device draws its own picture with its own renderer. If a game ever goes out of sync
  between devices, set the same RDP Plugin for that game on both (Emulator Options).
- The standalone dropped the Brick to 1x resolution and turned off hi-res textures during netplay; the new
  version keeps the normal settings (Mario Kart 64 stays at full speed on the Brick with Rice).
- Tested with 2 players (Brick ↔ Smart Pro S, 2026-10-10); 3–4 players are supported by the relay and the core
  but were not tested on hardware.

## Cleanup done by the update

- Removes `Emus/shared/mupen64plus` (the standalone's overlay settings, ini files and GLideN64 library) and the
  standalone's swap file. Player saves under `.userdata/shared/N64-mupen64plus` are left alone.
