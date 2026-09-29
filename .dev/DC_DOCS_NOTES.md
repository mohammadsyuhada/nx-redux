# Dreamcast on minarch: notes for the docs site

**Purpose:** a running list of user-visible Dreamcast changes from the move from standalone flycast to flycast's libretro core
on minarch. Use it to update the docs site (`~/Work/Personal/nx-redux-docs`) when the switch ships. Keep it current as each
sub-project lands; say for every item whether it is **built** (on branch `minarch-gpu-spike`) or **planned**.
Technical detail and measurements: `.dev/spikes/minarch-gpu/RESULTS.md`. Open work: `.dev/DEV_TODO.md` ("DC: …" entries).

Status: sub-projects 1 (core build), 2 (GPU path polish) and 3 (`DC.pak` on minarch) of 5 built. **Nothing below is released
yet**; the switch ships together with netplay (sub-project 4). Until then the current docs stay correct.

## Pages to update

### `docs/handheld/emulators/dreamcast.md`

- **Emulator** *(built, sub-project 3)*
  - Dreamcast runs on flycast **v2.7** as a libretro core inside minarch, like most other systems, instead of standalone
    flycast v2.6.
  - It gets minarch's features: the in-game menu, save-state slots with previews, auto-resume, the game switcher, fast-forward,
    screenshots, shaders and effects, playtime tracking.
  - Mention the performance figure from the benchmark (sub-project 5; Soulcalibur, libretro vs standalone, both devices).
    Spike numbers so far are at parity or better but are **not** for quoting.
- **Memory cards (VMU)** *(built)*
  - **One memory card per game**, in `Saves/DC/`, named after the ROM file: `Saves/DC/<rom name>.A1.bin` (e.g.
    `Soulcalibur (USA).A1.bin`). It replaces standalone's single shared card (`vmu_save_A1.bin` in the flycast data folder).
  - **Multi-disc games share one card:** a `(Disc N)` / `(Disc N of M)` tag is dropped from the name.
  - The same file name as NX Redux Mobile, so a card can be copied between phone and handheld.
  - Card writes are flushed immediately, so a crash or forced quit no longer corrupts the card.
    (Standalone and plain upstream flycast could leave a half-written card.)
  - Renaming a ROM file orphans its card (as with any save).
  - **Carried over from standalone** *(built, sub-project 3)*: on a game's first launch, its card starts as a copy of
    standalone's shared card (`.userdata/shared/DC-flycast/data/flycast/vmu_save_A1.bin`), so existing progress is kept.
    Also copied once when absent: console settings/clock (`dc_nvmem.bin`) and the second slot's card (`vmu_save_A2.bin`) to
    `Bios/DC/`; arcade settings and high scores (`<rom>.zip.nvmem`/`.nvmem2`/`.eeprom`) to `Saves/DC/reicast/`.
    The standalone files are never changed or deleted (they stay as a backup).
  - **Save states are not carried over** (different emulator version and format). Old standalone states stay on the card
    but can't be loaded; players should save in-game on the card before updating if they rely on states.
  - A game that never saved on the standalone card shows its "no save file" / create prompt as usual (e.g. Crazy Taxi 2).
  - Each copy happens **once**. To start a game with an empty card (or if its copy of an old, nearly full shared card has
    no room), delete `Saves/DC/<rom name>.A1.bin`: the next launch gets a fresh card and the old one is not copied back.
    A card already in `Saves/DC/` before the first launch (e.g. copied from NX Redux Mobile) is kept.
  - The standalone data folder `.userdata/shared/DC-flycast/` is kept for good, not cleaned up (it is small; minarch's
    Dreamcast save states also live in that folder).
- **BIOS** *(built, sub-project 3)*
  - Unchanged for users: `dc_boot.bin`, `naomi.zip`, `awbios.zip` stay in `Bios/DC/` (a RetroArch-style `Bios/DC/dc/` folder
    also works and then takes precedence).
  - The core also keeps `dc_nvmem.bin`, the shared second-slot card and a `data/` folder there.
  - Without `dc_boot.bin` the built-in HLE BIOS is used automatically (verified on the Smart Pro S).
- **Arcade (NAOMI / Atomiswave)** *(built)*
  - The modern MAME `awbios.zip` (containing `bios.ic23_l`) is accepted, as before (Metal Slug 6 verified).
- **Internal resolution** *(built default, sub-project 3)*
  - The default is 640×480 (Dreamcast native) on every device, the same as standalone.
  - Players can change it (`reicast_internal_resolution`, up to 1280×960 and beyond) in minarch's in-game Options (core
    options), per game or for all games; changing it mid-game is handled (framebuffer and aspect follow).
  - Higher values cost speed: the Brick has little headroom; the Smart Pro S may handle 960×720 in lighter games.
- **Frame rate and speed** *(built: minarch "Emulated" Core Sync)*
  - 30 fps games (Metal Slug 6, Quake III Arena) now run at their real speed.
  - Auto frame-skip stays on for heavy 3D games.
  - Widescreen (the hack) shows correctly at 16:9 on the Smart Pro S.
- **Debug HUD and ambient LEDs** *(built, sub-project 2)*
  - minarch's debug HUD (Options → Debug HUD) now works for Dreamcast. It has an extra **`EMU nn%`** line: true emulation
    speed, the best way to see whether a heavy scene keeps up.
  - Ambient LEDs follow Dreamcast games like other systems. Enabling it costs ~1–2 % speed on heavy scenes.
- **Controls** *(built, sub-project 3)*
  - Face buttons are position-based, as before: bottom = DC A, right = B, left = X, top = Y. L2/R2 = the analog triggers,
    Select = insert coin on arcade games (verified: Metal Slug 6 "CREDIT(S) 1").
  - Remapping is done in minarch's in-game Options → Controls, like other systems (standalone's mapping file is gone).
- **Netplay** *(built, sub-project 4; device E2E in progress)*
  - Same flow as other systems: Y / "Launch with Netplay" → the wizard (host or join, hotspot or Wi-Fi).
  - It's flycast's GGPO rollback netplay, now inside the libretro core.
  - **The host brings the save:**
    - the host's memory card, console settings and second card (or an arcade game's saves) are used for the session;
    - the other player plays on a temporary copy, so their own saves are never touched;
    - the host keeps a one-deep backup in `.userdata/shared/DC-flycast/netplay-backup/`.
  - **BIOS:** both use the real BIOS only when they have the same `dc_boot.bin`; otherwise both use the built-in (HLE) BIOS
    automatically. Arcade games need `naomi.zip` / `awbios.zip` on both devices.
  - **In-game menu during netplay:** MENU shows only **"Leave netplay?"** (A Leave, B Continue).
    - The other player's game pauses meanwhile.
    - With no answer in 20 s, the player leaves.
    - Save/load states, fast-forward, rewind and reset are off during a session (as for other netplay).
  - When a player leaves, the other device shows **"Netplay ended"** at once and returns to the game list. After a lost
    connection it takes ~25 s.
  - **Emulator Options → "Netplay Input Delay"** (0–5, default 1): each player's own setting. Higher means fewer
    corrections on a weak connection, but more input lag.
  - Kept the same on both sides for the session, whatever the settings (so the session stays in sync):
    - region, language, broadcast: follow the disc and the host's console settings;
    - SH4 clock at 200 MHz; DSP, widescreen cheats, fast GD-ROM loading and the 32 MB mod off; BBA/DCNet off.
  - Render settings (resolution, widescreen, frame skip) stay per device.
  - Fights run at ~70 % speed on this hardware, the same as standalone; menus at full speed. Expect some audio stutter.
- **Pre-launch Emulator Options** *(built, sub-project 3)*: the game-list "Emulator Options" entry and the Emulator
  Settings tool now edit flycast's core options (System, Video, Performance, Emulation Hacks, Input, Controller Expansion
  Slots, …), per game or for all games.
- **Arcade game names** *(built)*: arcade zips keep their full names in the game list (the table is regenerated from
  flycast v2.7), and minarch's in-game menu now shows that name too ("Metal Slug 6", not "mslug6"). The menu change also
  applies to FinalBurn Neo arcade zips. Arcade sets with upper-case `.ZIP`/`.7Z` names are handled like lower-case ones.
- **Removed with standalone** *(built)*: the standalone flycast in-game overlay (its own OSD menu with options,
  save/load and quit) is gone; minarch's in-game menu replaces it.
- **CPU / performance defaults** *(built)*: Performance CPU speed on both devices; on the Smart Pro S the emulation threads
  run on the big cores (as for PlayStation) and the GPU is set to performance.
- **RetroAchievements** *(built, sub-project 3; unlock check pending)*
  - Handled by minarch like other systems (no separate flycast login/CA bundle).
  - Check that the RA page's DC notes still hold.
- **Cheats** *(planned; check)*
  - DC may become eligible for minarch's cheat support. Verify before documenting.

### `docs/handheld/guide/in-game-options.md` (Core Sync)

- **Emulated** *(built)*: a new fourth value of **Core Sync**, after Auto / Screen / Native.
  - "Follows the game's own timing (GPU cores)."
  - The Dreamcast pak uses it by default *(built, sub-project 3)*. For other systems it behaves like Native.
  - Menu description text: "Emulated follows the game's own timing (GPU cores)."

### `docs/handheld/netplay.md`

- Update the Dreamcast section when sub-project 4 lands (see the Netplay notes above).

### `docs/handheld/emulators/cores.md`, `docs/handheld/emulators/index.md`

- The Dreamcast core row changes from standalone "flycast" to "flycast (libretro, v2.7)" *(built, sub-project 3)*.

### `docs/handheld/apps/retroachievements.md`, `docs/handheld/apps/cheats.md`

- Revisit their Dreamcast mentions after sub-project 3.

### `docs/reference/release-notes/handheld.md`

- Release-note bullets when the switch ships:
  - Dreamcast now on minarch;
  - per-game memory cards;
  - 30 fps games fixed;
  - the performance figure;
  - Emulated Core Sync;
  - widescreen aspect;
  - card-write safety.
- Credits: none needed (flycast was already credited, if it is). Separately, Kenney glyph credit is pending (see DEV_TODO).

## Changelog of this notes file

- 2026-09-29: sub-project 3 built (pak switch, save carry-over, BIOS dir, controls, options, arcade menu title, standalone
  build removed).
- 2026-09-29: created during sub-project 1. It covers the spike, phase 1a (AV-info / aspect), Emulated sync, the v2.7 core build
  and patches, the VMU fixes, and the netplay spike.
