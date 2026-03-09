# NX Redux

**My vision of how NextUI should be.**
Minimal on the surface. Structured underneath. Built to last.

[NextUI](https://github.com/LoveRetro/NextUI) is a custom firmware for retro handheld gaming devices. It replaces the stock operating system with a clean, minimal interface focused on playing retro games with no unnecessary bloat.

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Y8Y61SI04B)

![nextui-redux](https://raw.githubusercontent.com/mohammadsyuhada/NextUI-Redux/main/.github/resources/demo.gif)

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
- Refactored `nextui.c`, splitting the monolithic code into smaller, focused components.
- Applied various bug fixes and optimizations across the refactored components.
- Added clang-format tooling with enforced code style and VSCode support.
- Introduced reusable UI components for consistent design across tools.
- Fixed incorrect Wi-Fi/Bluetooth state icons in the quick menu.
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

New Features:
- Redesigned UI with consistent styling across the system.
- Game art fallback for titles without save states in the game switcher
- Main menu shortcut for quick access to frequently used `Tools` and `Games`
- Option to disable the emulator folders (ideal for users who prefer listing only selected games via shortcuts in the main menu)
- Direct selection of Wi-Fi networks and Bluetooth devices from the quick menu.
    - IP address is displayed in the bottom button hint bar when connected.
- Added `Developer options` in `Settings`:
    - Toggle SSH service and autostart
    - Disable system sleep (useful for ADB)
    - Clean up macOS-specific dotfiles (if any were copied)
    - Quickly turn off Developer Mode from the quick menu
- Added slide transition animations (can be disabled in Settings)
    - `Quick Menu` slides down on enter and up on exit
    - `Game Switcher` slides up on enter and down on exit
    - `Page Navigation` slides in from the right on enter and out to the left on exit
- Added `Simple Mode` in `Settings`
- Added `Search` function in main menu (Press `Y` to activate)
- Added jostick and calibration feature in `Input` app
- Added `Device Sync` to sync game saves, states, user settings, and ROMs (optional) across devices. 
- Added `Artwork Manager` to fetch custom mix box art for ROMs. 
- Built-in `Screenshot` and `Screen Recorder` option in quick menu
    - When Screenshot is enabled, press `L2` + `R2` + `X` to capture the screen
    - When Screen Recorder is enabled, recording runs automatically in the background.
- Built-in [Music Player](https://github.com/mohammadsyuhada/nextui-music-player)
- Built-in [Media Player](https://github.com/mohammadsyuhada/nextui-video-player)
- Bundled `Drastic Nintendo DS` emulator.
- Bundled `Mupen64Plus Nintendo 64` emulator.
    - Support for high resolution textures (with limitations due to 1GB RAM)
- Bundled `PPSSPP Playstation Portable` emulator.
- Bundled `Portmaster` in the Tools.
    - Configured by default with Nintendo input layout (configurable)

Upcoming Features:
- Built-in ROMs collection management
- Integration with [Netplay](https://github.com/mohammadsyuhada/nextui-netplay)
- Netplay for `Mupen64Plus Nintendo 64` emulator
- Netplay for `PPSSPP Playstation Portable` emulator 
- Minarch refactor

Ongoing focus areas:
- Cleaner, more maintainable core code
- Improved file and module structure
- Refactoring for readability
- Selective feature improvements as needed

## Upstream

This project is a derivative of [LoveRetro/NextUI](https://github.com/LoveRetro/NextUI).

Upstream changes may be merged selectively.
Architectural decisions here prioritize clarity and maintainability over strict parity.

## Credits

- [ro8inmorgan](https://github.com/ro8inmorgan), [frysee](https://github.com/frysee) and all contributors for developing NextUI
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
