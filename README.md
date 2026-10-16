# NX Redux

Custom firmware for TrimUI retro handhelds. NX Redux is a clean, pick-up-and-play way
to play retro games: pick up, pick a game, play. Home keeps your last game, this
month's play and your pinned favourites one press away, and you can switch any of it
off, down to a five-game screen. Underneath, emulators, netplay,
achievements, media tools and more wait in the Tools tab and pause menus until you
ask for them.

NX Redux is a fork of [NextUI](https://github.com/LoveRetro/NextUI) by LoveRetro, which itself descends from [MinUI](https://github.com/shauninman/MinUI).

📖 **User documentation: [nxredux.com](https://nxredux.com)** — [installation](https://nxredux.com/handheld/getting-started/), guides for every feature, a full settings reference and [release notes](https://nxredux.com/reference/release-notes/handheld/), illustrated with screenshots from the device.

An Android app for phones, tablets and Android handhelds is coming. It uses the same `Roms/`, `Bios/`, `Saves/` and `Collections/` folders, so one library can move between the handheld and the phone — see [nxredux.com/mobile](https://nxredux.com/mobile/getting-started/).

The video below demonstrates the features. It was recorded on an earlier release, so the current UI looks a little different and several newer features are not shown; the [documentation site](https://nxredux.com) has up-to-date screenshots.

[![nx-redux-youtube](https://github.com/user-attachments/assets/e4cf9c86-604b-49a4-b888-4b569ba592a9)](https://www.youtube.com/watch?v=l4iJBRgUe4U)

## Supported Devices

- **Trimui Brick** — firmware `1.1.1`
- **Trimui Brick Hammer** — firmware `1.1.1`
- **Trimui Brick Pro** — firmware `1.1.1` or `1.1.2`
- **Trimui Smart Pro S** — firmware `1.0.1` or `1.0.2`
- **Trimui Smart Pro** — firmware `1.1.1` (It should work in theory, but I can't confirm it because I don't have the device to test)

> ⚠️ **SD cards are built per device model.** Each release is packaged for a specific device — resolution, OSD assets and other layout differ between models — so a card set up for one device (e.g. the Brick) must **not** be moved into another (e.g. the Smart Pro S). To carry saves, save states and (optionally) ROMs across devices, over a USB cable or Wi-Fi, use the built-in **Device Sync** tool instead of swapping cards.

> ⚠️ **Update the stock firmware first.** NX Redux relies on system libraries the stock firmware ships, so older firmware breaks some features. Install the [official TrimUI firmware](https://github.com/trimui) version listed above for your device before installing NX Redux.

## Why Fork

NextUI keeps a deliberately tight core and pushes extras out to paks. That's the right call for a project many people contribute to — but it's not what I wanted to build. Some features only work the way I want them to when they live inside the system: sharing state with the core, drawing with the system's own UI, owning the input path. So this fork builds them in, takes on the maintenance cost that comes with that, and answers to no roadmap but mine.

## What's Different

Every feature below has its own page on [nxredux.com](https://nxredux.com); the list here is the map, not the manual.

Core experience:
- Redesigned UI with consistent styling across the system, slide transitions, scroll indicators, progress overlays and confirmation dialogs, with text sized per device and every menu scrolling at 60 fps.
- `Settings` rewritten in C, with the former `LED Control`, `Input`, `Clock` and `Updater` apps merged into it — [Settings reference](https://nxredux.com/handheld/settings/).
- A tabbed [main menu](https://nxredux.com/handheld/guide/main-menu/) — Home, Consoles, Collections and Tools. Home shows a Continue card for your last game, this month's play and your pinned games and tools, as a list, grid or carousel; the other tabs and game lists can be a List, Grid, Carousel or Backdrop, with box art, 3D box, wheel, mix or screenshot art ([Layouts](https://nxredux.com/handheld/settings/layouts/)). `START` opens [search](https://nxredux.com/handheld/guide/main-menu/#search).
- [Game Switcher](https://nxredux.com/handheld/guide/game-switcher/) lists only resumable games by default, with art fallback and an auto-save on quit so every game resumes where you left it.
- A [game context menu](https://nxredux.com/handheld/guide/context-menu/): collections, pin to Home, rename, delete, remove from recents, refresh, per-game emulator options, and [Launch Options](https://nxredux.com/handheld/guide/launch-options/) (CPU and GPU settings) for ports.
- [Simple Mode](https://nxredux.com/handheld/guide/simple-mode/) — a PIN-protected, distraction-free menu for children or casual users.

Available when you want it (Tools, pause menu and OSD):
- [On-Screen Display](https://nxredux.com/handheld/guide/osd/) reachable anywhere: mute, brightness, motor, Wi-Fi, Bluetooth and LED toggles, screenshot and screen recorder, system monitors, music controls and power off.
- [Music Player](https://nxredux.com/handheld/apps/music-player/) with background playback, internet radio, podcasts, embedded tags, cover art and lyrics, and hi-res output on USB DACs, plus a [Media Player](https://nxredux.com/handheld/apps/media-player/) with audio and subtitle switching.
- Built-in [PSP](https://nxredux.com/handheld/emulators/psp/), [Nintendo 64](https://nxredux.com/handheld/emulators/nintendo-64/), [Dreamcast](https://nxredux.com/handheld/emulators/dreamcast/) (plus a lighter Dreamcast Lite) and a second, GPU-rendered [PlayStation](https://nxredux.com/handheld/emulators/playstation/) system (SwanStation), all running inside NX Redux's own emulator with the in-game menu, save states with screenshots, RetroAchievements and the Game Switcher. [Nintendo DS](https://nxredux.com/handheld/emulators/nintendo-ds/) runs on a bundled standalone DraStic with the same in-game menu. Every emulator supports USB-C and Bluetooth audio and sleep on the power button.
- [PortMaster](https://nxredux.com/handheld/apps/portmaster/) with the NextOS Ports and RHH Ports community catalogs, plus extra games, installable on-device from the [Xtras Store](https://nxredux.com/handheld/apps/xtras/).
- [Netplay](https://nxredux.com/handheld/netplay/) for local multiplayer over Wi-Fi, a hotspot or a USB cable between two devices: press `Y` on a supported game (or on Home) to host or join. Includes Game Boy link, a real Game Boy Advance link cable over USB (gpSP and mGBA), lockstep netplay for the other cores, up to 4-player Nintendo 64 and GGPO rollback for Dreamcast.
- [RetroAchievements](https://nxredux.com/handheld/apps/retroachievements/) with full offline support — earn achievements offline, sync later, and browse your whole cached library on the device. Softcore only by design.
- [Cheats](https://nxredux.com/handheld/apps/cheats/) downloaded straight from the libretro cheat database, no PC needed.
- [Device Sync](https://nxredux.com/handheld/apps/device-sync/) for saves, states and ROMs across devices (USB cable or Wi-Fi), [Artwork Manager](https://nxredux.com/handheld/apps/artwork-manager/) for box art, wheels, mixes and image optimizing, and a redesigned [Game Tracker](https://nxredux.com/handheld/apps/game-tracker/).
- [Nintendo / Xbox button layout](https://nxredux.com/handheld/guide/button-layout/), joystick calibration, and [Developer options](https://nxredux.com/handheld/settings/developer/) with SSH, sleep control and debug logging.

Under the hood:
- `nextui.c` split into focused components (game list, game switcher, search, launcher, image loader and more) and `minarch` split into `ma_*` modules, adapted from [carroarmato0's work in NextUI #721](https://github.com/LoveRetro/NextUI/pull/721).
- A reusable UI component library in `common/ui/`, one component per file, wired into every app through a single `ui.mk` fragment.
- Sink-aware audio sample-rate negotiation across music, radio, video and every emulator, with automatic rerouting when a USB-C DAC or Bluetooth device connects — see [Audio settings](https://nxredux.com/handheld/settings/audio/).
- clang-format tooling with enforced code style, and per-device release zips (brick / brickpro / smartpro / smartpros).

Removed or consolidated:
- The `Remove Loading` tweak now happens once in the install script instead of through a resident app.
- The `Battery` history feature is gone — it needed an always-on daemon logging to the SD card, while the status bar already shows the charge level.
- Hardcore RetroAchievements mode is intentionally omitted — NX Redux is not an RA-approved hardcore emulator, so leaving it out keeps your account safe.

## Additional Emulators

Emulators that aren't bundled can be added by copying a community pak into the `/Emus` folder on the SD card. Give it its own name: a pak with the same name as a shipped one is ignored. See [Additional Emulators](https://nxredux.com/handheld/emulators/additional/).

> ⚠️ Community paks are built for **NextUI**, not for NX Redux. They generally work, but they are not developed, maintained or supported for NX Redux — please don't report NX Redux-specific problems to their developers.

## Upstream

This project is a derivative of [LoveRetro/NextUI](https://github.com/LoveRetro/NextUI), forked while NextUI was licensed under GPL-3.0.

NX Redux now develops independently. NextUI has since moved to the PolyForm Noncommercial 1.0.0 license, which is not compatible with this project's GPL-3.0 — so newer upstream changes are no longer merged, and NX Redux continues from the GPL-3.0 codebase it forked.

## AI-assisted development

NX Redux is developed with the help of [Claude](https://claude.ai), Anthropic's AI model. The maintainer's own contributions since the fork — features, fixes and this documentation — are written by Claude from the maintainer's requirements and feedback, then tested on real hardware and reviewed before shipping. This does not cover the inherited NextUI and MinUI code or other people's contributions. Full details on the [disclaimer page](https://nxredux.com/reference/disclaimer/).

## Credits

- [ro8inmorgan](https://github.com/ro8inmorgan), [frysee](https://github.com/frysee) and all contributors for developing NextUI
- [clintonium-119](https://github.com/clintonium-119) for the original [RetroAchievements integration](https://github.com/LoveRetro/NextUI/pull/633) in NextUI that our offline support is built on
- [carroarmato0](https://github.com/carroarmato0) for the [minarch modularization](https://github.com/LoveRetro/NextUI/pull/721) that the `ma_*` split here is based on
- [sinedied](https://github.com/sinedied/perfect-retroshaders) for the shaders that were originally proposed for NextUI in [LoveRetro/NextUI#796](https://github.com/LoveRetro/NextUI/pull/796).
- [KrutzOtrem](https://github.com/KrutzOtrem/Trimui-Brick-Overlays) for the overlays
- [ben16w](https://github.com/ben16w/minui-portmaster) for the Minui-Portmaster, and [PortMaster](https://portmaster.games/) for the community game-port launcher and catalog
- The authors of every emulator core NX Redux runs, and [RetroAchievements](https://retroachievements.org/) / [rcheevos](https://github.com/RetroAchievements/rcheevos) for the achievements ecosystem
- Dan Patrick for the [redrawn console logos](https://archive.org/details/console-logos-professionally-redrawn-plus-official-versions), Xiaomi for the [MiSans](https://hyperos.mi.com/font/en/download/) font, and the [M+ FONTS Project](https://mplusfonts.github.io/) and Jikasei Font Koubou for the [Rounded M+ 1c](http://jikasei.me/font/rounded-mplus/) font

## License

Licensed under **GNU GPL v3.0**, the license NextUI was under at the time of the fork.

All original copyrights are retained.
Modifications in this repository are also distributed under GPL-3.0.

See the [LICENSE](LICENSE) file for details.


> *NX Redux is an independent fork and is not affiliated with the original NextUI project.*
