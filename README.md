# NX Redux

**My vision of how NextUI should be.**
Minimal on the surface. Structured underneath. Built to last.

[NextUI](https://github.com/LoveRetro/NextUI) is a custom firmware for retro handheld gaming devices. It replaces the stock operating system with a clean, minimal interface focused on playing retro games with no unnecessary bloat.

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Y8Y61SI04B)


Refer to the Youtube Video below for demonstration of the features:
[![nx-redux-youtube](https://github.com/user-attachments/assets/e4cf9c86-604b-49a4-b888-4b569ba592a9)](https://www.youtube.com/watch?v=l4iJBRgUe4U)

## Supported Devices

- **Trimui Brick**
- **Trimui Smart Pro**
- **Trimui Smart Pro S**

## Why Fork?

NextUI is a great foundation — lightweight, focused, and true to its minimalist roots.

But over time I found myself wanting two things the upstream project couldn't give me:

1. **Creative freedom** — the ability to add features and UX improvements without waiting for upstream approval or aligning with someone else's roadmap.
2. **Structural clarity** — cleaner code organization, consistent formatting, and a codebase that's easier to maintain and extend.

NX Redux is where those two goals meet.

## What's Different

Improvements:
- Refactored `nextui.c`, splitting the monolithic code into smaller, focused components (game list, game switcher, search, launcher, image loader and more).
- Refactored `minarch.c`, splitting the ~9,000-line monolith into focused `ma_*` modules (game, saves, rewind, config, shaders, options, input, video, audio, core, menu and more).
- Applied various bug fixes and optimizations across the refactored components.
- Added clang-format tooling with enforced code style and VSCode support.
- Introduced a reusable UI component library in `common/ui/` for consistent design across tools.
    - Strict one-component-per-file layout (menu bar, button hint bar, dialogs, overlays, lists, keyboard, toast, etc.), each with its own header — apps include only what they use.
    - Single `ui.mk` fragment wires the components into every app build, so adding a component is a one-line change.
    - Shared drawing primitives (rounded rects/pills, scrim surfaces, centered button rows) replace previously duplicated rendering code across the UI files.
- Added a semi-transparent progress overlay for blocking actions.
- Added confirmation dialogs for actions that require them.
- Rewrote the `Settings` app in C with a redesigned UI
- Removed the `Battery` monitoring feature. 
- Merged the `LED Control`, `Input`, `Clock` and `Updater` into the `Settings` app (no separate app required)
- Integrated the `Remove Loading` feature directly into the install script (no separate app required) 
- Split release builds into per-platform zips packages (brick/smartpro/smartpros).
- All standalone emulators now support USB-C and Bluetooth audio. 
- All standalone emulators now include a custom in-game menu with UI styling consistent with the system.
- All standalone emulators now support save states with screenshots.
- Added sleep by pressing power button support for all standalone emulator and Portmaster games. 
- Added top and bottom scroll indicators to the all menu list.
- Redesigned the `Game Tracker` tool with a cleaner play-stats list (total · average · play count per game).
    - Any game's play record can now be deleted (press `X`, with a confirmation dialog). The record starts fresh the next time the game is played.

New Features:
- Redesigned UI with consistent styling across the system.
- Game art fallback for titles without save states in the game switcher
- Main menu shortcut for quick access to frequently used `Tools` and `Games`
- Option to disable the emulator folders (ideal for users who prefer listing only selected games via shortcuts in the main menu)
- Direct selection of Wi-Fi networks and Bluetooth devices from `Settings`.
    - IP address is displayed in the bottom button hint bar when connected.
- Added `Developer options` in `Settings`:
    - Toggle SSH service and autostart
    - Disable system sleep (useful for ADB)
    - Clean up macOS-specific dotfiles (if any were copied)
- Added slide transition animations (can be disabled in Settings)
    - `Game Switcher` slides up on enter and down on exit
    - `Page Navigation` slides in from the right on enter and out to the left on exit
- Added `Simple Mode` in `Settings` — a simplified menu for children or casual users.
    - Hides `Tools` from the main menu and replaces `Options` with `Reset` in-game.
    - `Settings` stays on the main menu, protected by a 4-digit PIN set when enabling Simple Mode.
    - Forgot the PIN? Delete `.userdata/shared/enable-simple-mode` from the SD card to turn Simple Mode off.
- Added `Search` function in main menu (Press `Y` to activate)
- Added a game-list context menu (press `MENU` on a highlighted game):
    - Built-in ROMs collection management — add a game to an existing collection or create a new one on the spot.
    - Pin or unpin a game to the main menu.
    - Rename a game — its box art, saves and save states are renamed along with it.
    - Delete a game.
    - Remove a game from `Recently Played`.
    - Refresh the ROMs list.
- Added jostick and calibration feature in `Input` app
- Added `Device Sync` to sync game saves, states, user settings, and ROMs (optional) across devices. 
- Added `Artwork Manager` to fetch custom mix box art for ROMs. 
- Added `On-Screen Display (OSD)` for quick access to common actions from anywhere — in the menus or in-game.
    - Opened with the `Home` button on devices that have one (Smart Pro S), or by long-pressing the `MENU` button (Brick / Smart Pro).
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
- Built-in [Media Player](https://github.com/mohammadsyuhada/nextui-video-player)
- Bundled `Drastic Nintendo DS` emulator.
- Bundled `Mupen64Plus Nintendo 64` emulator.
    - Support for high resolution textures (with limitations due to 1GB RAM)
- Bundled `Portmaster` in the Tools.
    - Configured by default with Nintendo input layout (configurable)
- Added [Netplay](https://github.com/mohammadsyuhada/nextui-netplay) for local wireless multiplayer, available from the in-game menu.
    - Host or join over a regular Wi-Fi network, or let the host device start its own hotspot (no router needed).
    - `GB Link` support for Game Boy (gambatte) — link cable games like Pokémon trades and battles.
    - `GBA Link` support for Game Boy Advance (gpSP) — wireless adapter and link cable games.
    - Classic lockstep netplay for the other supported cores.
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

Upcoming Features:
- `CPU mode` switch in the OSD — quickly change the CPU governor (e.g. performance mode) from anywhere; defaults to auto.
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
