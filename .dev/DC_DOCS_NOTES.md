# Dreamcast on minarch: notes for the docs site

**Purpose:** a running list of user-visible Dreamcast changes from the move from standalone flycast to flycast's libretro core
on minarch. Use it to update the docs site (`~/Work/Personal/nx-redux-docs`) when the switch ships. Keep it current as each
sub-project lands; say for every item whether it is **built** (on branch `minarch-gpu-spike`) or **planned**.
Technical detail and measurements: `.dev/spikes/minarch-gpu/RESULTS.md`. Open work: `.dev/DEV_TODO.md` ("DC: …" entries).

Status: sub-projects 1 (core build) and 2 (GPU path polish) of 5 done. **Nothing below is released yet.** Until `DC.pak` switches (sub-project 3),
users still run standalone flycast, and the current docs stay correct.

## Pages to update

### `docs/handheld/emulators/dreamcast.md`

- **Emulator** *(planned, sub-project 3)*
  - Dreamcast runs on flycast **v2.7** as a libretro core inside minarch, like most other systems, instead of standalone
    flycast v2.6.
  - It gets minarch's features: the in-game menu, save-state slots with previews, auto-resume, the game switcher, fast-forward,
    screenshots, shaders and effects, playtime tracking.
  - Mention the performance figure from the benchmark (sub-project 5; Soulcalibur, libretro vs standalone, both devices).
    Spike numbers so far are at parity or better but are **not** for quoting.
- **Memory cards (VMU)** *(built into the core; the default setting is planned)*
  - **One memory card per game**, in `Saves/DC/`, named after the ROM file: `Saves/DC/<rom name>.A1.bin` (e.g.
    `Soulcalibur (USA).A1.bin`). It replaces standalone's single shared card (`vmu_save_A1.bin` in the flycast data folder).
  - **Multi-disc games share one card:** a `(Disc N)` / `(Disc N of M)` tag is dropped from the name.
  - The same file name as NX Redux Mobile, so a card can be copied between phone and handheld.
  - Card writes are flushed immediately, so a crash or forced quit no longer corrupts the card.
    (Standalone and plain upstream flycast could leave a half-written card.)
  - Renaming a ROM file orphans its card (as with any save).
  - **Migration of existing cards/states from standalone:** *(planned, sub-project 3; describe once decided.)*
- **BIOS** *(planned, sub-project 3)*
  - The libretro core looks in `Bios/DC/dc/` (`dc_boot.bin`, `naomi.zip`, `awbios.zip`). Document where users put the files,
    or whether the pak copies/links from `Bios/DC/`, once decided.
  - The HLE BIOS still works without `dc_boot.bin`.
- **Arcade (NAOMI / Atomiswave)** *(built)*
  - The modern MAME `awbios.zip` (containing `bios.ic23_l`) is accepted, as before (Metal Slug 6 verified).
- **Internal resolution** *(planned default, sub-project 3)*
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
- **Controls** *(planned, sub-project 3)*
  - Document the final mapping (position-based face buttons, Select = coin for arcade, analog triggers if added) and the
    minarch Controls menu instead of standalone's mapping file.
- **Netplay** *(planned, sub-project 4)*
  - GGPO netplay works on the libretro core (spike verified).
  - Document the same wizard flow, any change in which device is host (player 1) / client (player 2), and the note that
    **opening the in-game menu for more than ~3 s drops the session**.
  - Fights run at about 70 % speed on this hardware in both the standalone and libretro builds.
- **RetroAchievements** *(planned, sub-project 3)*
  - Handled by minarch like other systems (no separate flycast login/CA bundle).
  - Check that the RA page's DC notes still hold.
- **Cheats** *(planned; check)*
  - DC may become eligible for minarch's cheat support. Verify before documenting.

### `docs/handheld/guide/in-game-options.md` (Core Sync)

- **Emulated** *(built)*: a new fourth value of **Core Sync**, after Auto / Screen / Native.
  - "Follows the game's own timing (GPU cores)."
  - The Dreamcast pak uses it by default *(planned, sub-project 3)*. For other systems it behaves like Native.
  - Menu description text: "Emulated follows the game's own timing (GPU cores)."

### `docs/handheld/netplay.md`

- Update the Dreamcast section when sub-project 4 lands (see the Netplay notes above).

### `docs/handheld/emulators/cores.md`, `docs/handheld/emulators/index.md`

- The Dreamcast core row changes from standalone "flycast" to "flycast (libretro, v2.7)" *(planned, sub-project 3)*.

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

- 2026-09-29: created during sub-project 1. It covers the spike, phase 1a (AV-info / aspect), Emulated sync, the v2.7 core build
  and patches, the VMU fixes, and the netplay spike.
