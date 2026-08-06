# NX Redux

Custom firmware for retro handheld gaming devices. It keeps the minimal,
distraction-free interface — pick up, pick a game, play — while deliberately extending
what sits underneath: standalone emulators, netplay, achievements, media tools and more.
Those extras stay out of the way until you ask for them, tucked into the Tools and pause
menus (and hidden entirely in simple mode).

NX Redux is a fork of [NextUI](https://github.com/LoveRetro/NextUI) by LoveRetro, which itself descends from [MinUI](https://github.com/shauninman/MinUI).

Refer to the Youtube Video below for demonstration of the features:

[![nx-redux-youtube](https://github.com/user-attachments/assets/e4cf9c86-604b-49a4-b888-4b569ba592a9)](https://www.youtube.com/watch?v=l4iJBRgUe4U)

## Supported Devices

- **Trimui Brick**
- **Trimui Brick Hammer**
- **Trimui Brick Pro** 
- **Trimui Smart Pro S**
- **Trimui Smart Pro** (It should work in theory, but I can't confirm it because I don't have the device to test)

> ⚠️ **SD cards are built per device model.** Each release is packaged for a specific device — resolution, OSD assets and other layout differ between models — so a card set up for one device (e.g. the Brick) must **not** be moved into another (e.g. the Smart Pro S). To carry saves, save states, settings and (optionally) ROMs across devices, use the built-in **Device Sync** tool instead of swapping cards.

## Why Fork

NextUI keeps a deliberately tight core and pushes extras out to paks. That's the right call for a project many people contribute to — but it's not what I wanted to build. Some features only work the way I want them to when they live inside the system: sharing state with the core, drawing with the system's own UI, owning the input path. So this fork builds them in, takes on the maintenance cost that comes with that, and answers to no roadmap but mine.

## What's Different

Core experience:
- Redesigned UI with consistent styling across the system.
- Rewrote the `Settings` app in C with a redesigned UI.
- Game art fallback for titles without save states in the game switcher.
- `Game Switcher` lists only resumable games by default — switch to `All recent games` in `Settings → System`.
- Quitting a game now auto-saves to a hidden save slot (minarch cores, N64 and Dreamcast), so the `Game Switcher` always resumes exactly where you left off.
- Main menu shortcut for quick access to frequently used `Tools` and `Games`
- Option to disable the emulator folders (ideal for users who prefer listing only selected games via shortcuts in the main menu)
- Added `Search` function in main menu (Press `START` to activate)
- Added a game-list context menu (press `MENU` on a highlighted game):
    - Built-in ROMs collection management — add a game to an existing collection or create a new one on the spot.
    - Pin or unpin a game to the main menu.
    - Rename a game — its box art, saves and save states are renamed along with it.
    - Delete a game.
    - Remove a game from `Recently Played`.
    - Refresh the ROMs list.
    - Edit per game emulator options
- Added slide transition animations (can be disabled in Settings)
    - `Game Switcher` slides up on enter and down on exit
    - `Page Navigation` slides in from the right on enter and out to the left on exit
- Added top and bottom scroll indicators to all menu lists.
- Added a semi-transparent progress overlay for blocking actions.
- Added confirmation dialogs for actions that require them.
- Added `Simple Mode` in `Settings` — a simplified menu for children or casual users.
    - Hides `Tools` from the main menu and replaces `Options` with `Reset` in-game.
    - `Settings` stays on the main menu, protected by a 4-digit PIN set when enabling Simple Mode.
    - Forgot the PIN? Delete `.userdata/shared/enable-simple-mode` from the SD card to turn Simple Mode off.

Available when you want it (Tools, pause menu and OSD):
- Added `On-Screen Display (OSD)` for quick access to common actions from anywhere — in the menus or in-game.
    - Opened with the `Home` button on devices that have one (Smart Pro S), or by long-pressing the `MENU` button (Brick / Brick Pro / Smart Pro).
    - Volume slider with mute toggle, brightness slider, and rumble toggle.
    - Wi-Fi, Bluetooth and LED toggles with live state.
    - Built-in `Screenshot` and `Screen Recorder`:
        - When Screenshot is enabled, press `L2` + `R2` to capture the screen — an on-screen hint shows the shortcut, and a toast confirms each saved capture.
        - When Screen Recorder is enabled, recording runs automatically in the background (the record icon turns red while recording).
        - The OSD closes itself when either is activated, so it never gets in the way of the capture.
    - System monitors: CPU frequency, memory usage and temperature (plus fan control on the Smart Pro S).
    - Power off button.
    - The entire OSD (layout, widgets, icons) ships on the SD card, so it stays consistent regardless of the stock firmware version.
- Built-in [Music Player](https://github.com/mohammadsyuhada/nextui-music-player)
    - Audio settings: sample-rate mode (`Device default` / `Follow source` for bit-exact hi-res playback on USB DACs), resampler quality and buffer size.
    - Live sample-rate badge on the now-playing screen (e.g. `96kHz` when playing natively, `44.1→48kHz` when resampling).
    - Internet radio streams through the same high-quality resampler, with cover art fetched for the currently playing song.
- Built-in `Media Player` with audio and subtitle switcher.
- Bundled `Drastic Nintendo DS` emulator.
    - Hold `SELECT` + `Left` / `Right` to cycle the screen layout.
    - Hold `SELECT` + `Y` to cycle the theme.
- Bundled `Mupen64Plus Nintendo 64` emulator.
    - Support for high resolution textures (with limitations due to 1GB RAM)
        - Place Rice-format texture packs in `Roms/Nintendo 64 (N64)/.hires_texture/<ROM NAME>/`, where `<ROM NAME>` is the ROM's **internal header name** (e.g. `MARIOKART64`), not its filename. To find it, run the game once and look for the `Core: Name:` line in `.userdata/<platform>/logs/N64.txt`.
        - On the game's first launch the pack is converted into a cache in `Roms/Nintendo 64 (N64)/.cache/` with an on-screen progress display — large packs take several minutes and need extra free space on the SD card (e.g. a 2.6 GB pack produces a ~450 MB cache). Later launches load straight from the cache and start fast.
    - Netplay with up to 4 players for Nintendo 64 games:
        - **Player-count depends on the device's GPU.** N64 renders a separate split-screen viewport per player, so 3–4 players need a **Smart Pro S** on *every* seat. On the **Smart Pro / Brick / Brick Pro** the GPU can't hold full speed past a 2-way split, so N64 netplay there is limited to **2 players** (as host or joiner).
- Bundled `Flycast Sega Dreamcast` emulator.
    - Runs out of the box without a BIOS (HLE boot); drop `dc_boot.bin` into `Bios/DC/` on the SD card to boot through the real BIOS instead.
    - GGPO netplay for Dreamcast games, up to 2 players.
- Bundled `Portmaster` in the Tools.
    - Configured by default with Nintendo input layout (configurable)
- All standalone emulators now support USB-C and Bluetooth audio. 
- All standalone emulators now include a custom in-game menu with UI styling consistent with the system.
- All standalone emulators now support save states with screenshots.
- Added sleep by pressing power button support for all standalone emulator and Portmaster games. 
- Added [Netplay](https://github.com/mohammadsyuhada/nextui-netplay) for local wireless multiplayer. Press `Y` on a supported game in the list to host or join over Wi-Fi or a device-hosted hotspot — no manual IP entry, no persistent toggle to remember to turn back off, and save data is synced automatically before the match starts.
    - `GB Link` support for Game Boy (gambatte) — link cable games like Pokémon trades and battles.
    - `GBA Link` support for Game Boy Advance (gpSP) — wireless adapter and link cable games.
    - Classic lockstep netplay for the other supported cores.
    - Supported `Dreamcast` and `Nintendo 64` standalone emulator.
    - Save states, fast-forward and rewind are automatically disabled during a session to protect the connection.
- Added `RetroAchievements` with full offline support (powered by [rcheevos](https://github.com/RetroAchievements/rcheevos)).
    - Earn achievements while completely offline — they are journaled to the SD card and submitted automatically the next time you play online.
    - Achievement data (definitions, unlock state and badges) is cached as you play and can be pre-downloaded for your whole library, so games work offline even if you have never launched them online before.
    - Softcore only by design — NX Redux is not an RA-approved hardcore emulator, so hardcore mode is intentionally omitted to keep your account safe.
    - New `RetroAchievements` tool as the single home for the feature:
        - Sign in and manage all achievement settings here (moved out of `Settings`).
        - Browse every cached game and its achievements — unlocked, pending-sync and locked — with box art and badges, fully offline.
        - Per-achievement details: description, points, unlock date, unlock rate and type (Progression / Win Condition / Missable), honouring your chosen sort order.
        - `Sync now` to push pending offline unlocks, and `Download all game data` to cache your whole library with a live progress bar.
        - `Reset account data` (for switching accounts) and `Erase all achievement data` options.
    - In-game achievement unlock and progress notifications, with a per-achievement mute toggle.
- Added `Device Sync` to sync game saves, states, user settings, and ROMs (optional) across devices. 
- Added `Artwork Manager` to fetch custom mix box art for ROMs. 
- Redesigned the `Game Tracker` tool with a cleaner play-stats list (total · average · play count per game).
    - Any game's play record can now be deleted (press `X`, with a confirmation dialog). The record starts fresh the next time the game is played.
- Added joystick calibration in `Settings → Input`.
- Added `Developer options` in `Settings`:
    - Toggle SSH service and autostart
    - Disable system sleep (useful for ADB)
    - Keep device awake over USB
    - Clean up macOS-specific dotfiles (if any were copied)

Under the hood:
- Refactored `nextui.c`, splitting the monolithic code into smaller, focused components (game list, game switcher, search, launcher, image loader and more).
- Introduced a reusable UI component library in `common/ui/` for consistent design across tools.
    - Strict one-component-per-file layout (menu bar, button hint bar, dialogs, overlays, lists, keyboard, toast, etc.), each with its own header — apps include only what they use.
    - Single `ui.mk` fragment wires the components into every app build, so adding a component is a one-line change.
    - Shared drawing primitives (rounded rects/pills, scrim surfaces, centered button rows) replace previously duplicated rendering code across the UI files.
- Applied various bug fixes and optimizations across the refactored components.
- Added clang-format tooling with enforced code style and VSCode support.
- Split release builds into per-platform zips packages (brick/brickpro/smartpro/smartpros).
- Sink-aware audio sample-rate negotiation across the whole system (music, radio, video and all emulators):
    - The audio device opens at the rate the active output actually prefers — 48 kHz on the speaker, the negotiated Bluetooth rate, or a USB DAC's supported rates — with high-quality in-app resampling instead of silent low-quality system resampling.
    - Hot-plugging a USB-C DAC or connecting Bluetooth mid-playback reroutes audio automatically, and a headphone icon appears in the status bar while an external output is active.
    - New `Audio` page in `Settings`: current output device and rate, a `Force 48 kHz` escape hatch, and the Bluetooth max sample rate setting.

Removed or consolidated:
- Merged the standalone `LED Control`, `Input`, `Clock` and `Updater` apps into the `Settings` app — one place to configure the device instead of five separate paks.
- Integrated the `Remove Loading` feature directly into the install script — it's a one-time tweak, not something that needs a resident app.
- Removed the `Battery` monitoring feature — it needed an always-on daemon logging to a database on the SD card to power a history graph, while the status bar already shows the charge level.
- Hardcore RetroAchievements mode is intentionally omitted (see the RetroAchievements notes above — account safety).

Merged from upstream:
- Modular `minarch` split (`ma_*` modules: game, saves, rewind, config, shaders, options, input, video, audio, core, menu and more) — from [carroarmato0's work in NextUI #721](https://github.com/LoveRetro/NextUI/pull/721), adapted here.

Upcoming Features:
- `CPU mode` switch in the OSD — quickly change the CPU governor (e.g. performance mode) from anywhere; defaults to mode configured in launch.sh script.
- Background `Music Player` — keep music playing while you browse, with playback controls in the OSD.

Ongoing focus areas:
- Cleaner, more maintainable core code
- Improved file and module structure
- Refactoring for readability
- Selective feature improvements as needed

## Additional Emulators

Some emulators are not bundled with NX Redux. If you want to run PPSSPP — or any other system that isn't included — you can install a community pak instead:

- **PPSSPP** (PlayStation Portable) — [ben16w/minui-psp](https://github.com/ben16w/minui-psp)

> ⚠️ These paks are built for **NextUI** (which NX Redux is based on), **not** for NX Redux. They will generally work, but they are not developed, maintained, or supported for NX Redux. Please **do not** report issues you hit while using them on NX Redux to their developers — those developers build for NextUI and cannot help with NX Redux-specific behavior.

## Upstream

This project is a derivative of [LoveRetro/NextUI](https://github.com/LoveRetro/NextUI).

Upstream changes may be merged selectively.
Architectural decisions here prioritize clarity and maintainability over strict parity.

## Credits

- [ro8inmorgan](https://github.com/ro8inmorgan), [frysee](https://github.com/frysee) and all contributors for developing NextUI
- [clintonium-119](https://github.com/clintonium-119) for the original [RetroAchievements integration](https://github.com/LoveRetro/NextUI/pull/633) in NextUI that our offline support is built on
- [carroarmato0](https://github.com/carroarmato0) for the [minarch modularization](https://github.com/LoveRetro/NextUI/pull/721) that the `ma_*` split here is based on
- [KrutzOtrem](https://github.com/KrutzOtrem/Trimui-Brick-Overlays) for the overlays
- [timbueno](https://github.com/timbueno/ArtBookNextUI.theme) for the Artbook theme
- [anthonycaccese](https://github.com/anthonycaccese/art-book-next-es.git) for the Artbook artwork
- [ben16w](https://github.com/ben16w/minui-portmaster) for the Minui-Portmaster

## License

Licensed under **GNU GPL v3.0**, the same license as the original project.

All original copyrights are retained.
Modifications in this repository are also distributed under GPL-3.0.

See the [LICENSE](LICENSE) file for details.


> *NX Redux is an independent fork and is not affiliated with the original NextUI project.*
