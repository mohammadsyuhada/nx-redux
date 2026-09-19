# NX Redux

Custom firmware for retro handheld gaming devices. It keeps the minimal,
distraction-free interface — pick up, pick a game, play — while deliberately extending
what sits underneath: standalone emulators, netplay, achievements, media tools and more.
Those extras stay out of the way until you ask for them, tucked into the Tools and pause
menus (and hidden entirely in simple mode).

NX Redux is a fork of [NextUI](https://github.com/LoveRetro/NextUI) by LoveRetro, which itself descends from [MinUI](https://github.com/shauninman/MinUI).

📖 **User documentation: [nxredux.com](https://nxredux.com)** — installation, guides for every feature and a full settings reference, illustrated with screenshots from the device.

The video below demonstrates the features. It was recorded on an earlier release, so the current UI looks a little different and several newer features are not shown; the [documentation site](https://nxredux.com) has up-to-date screenshots.

[![nx-redux-youtube](https://github.com/user-attachments/assets/e4cf9c86-604b-49a4-b888-4b569ba592a9)](https://www.youtube.com/watch?v=l4iJBRgUe4U)

## Supported Devices

- **Trimui Brick** — firmware `1.1.1`
- **Trimui Brick Hammer** — firmware `1.1.1`
- **Trimui Brick Pro** — firmware `1.1.1`
- **Trimui Smart Pro S** — firmware `1.0.1` or `1.0.2`
- **Trimui Smart Pro** — firmware `1.1.1` (It should work in theory, but I can't confirm it because I don't have the device to test)

> ⚠️ **SD cards are built per device model.** Each release is packaged for a specific device — resolution, OSD assets and other layout differ between models — so a card set up for one device (e.g. the Brick) must **not** be moved into another (e.g. the Smart Pro S). To carry saves, save states, settings and (optionally) ROMs across devices, use the built-in **Device Sync** tool instead of swapping cards.

> ⚠️ **Update the stock firmware first.** NX Redux relies on system libraries the stock firmware ships, so older firmware breaks some features. Install the [official TrimUI firmware](https://github.com/trimui) version listed above for your device before installing NX Redux.

## Why Fork

NextUI keeps a deliberately tight core and pushes extras out to paks. That's the right call for a project many people contribute to — but it's not what I wanted to build. Some features only work the way I want them to when they live inside the system: sharing state with the core, drawing with the system's own UI, owning the input path. So this fork builds them in, takes on the maintenance cost that comes with that, and answers to no roadmap but mine.

## What's Different

Every feature below has its own page on [nxredux.com](https://nxredux.com); the list here is the map, not the manual.

Core experience:
- Redesigned UI with consistent styling across the system, slide transitions, scroll indicators, progress overlays and confirmation dialogs.
- `Settings` rewritten in C, with the former `LED Control`, `Input`, `Clock` and `Updater` apps merged into it — [Settings reference](https://nxredux.com/settings/).
- [Game Switcher](https://nxredux.com/guide/game-switcher/) lists only resumable games by default, with art fallback and an auto-save on quit so every game resumes where you left it.
- Main menu [shortcuts](https://nxredux.com/guide/main-menu/) for frequently used tools and games, optional hiding of the emulator folders, and `START` to [search](https://nxredux.com/guide/main-menu/).
- A [game context menu](https://nxredux.com/guide/context-menu/): collections, pin to main menu, rename, delete, remove from recents, refresh, per-game emulator options.
- [Simple Mode](https://nxredux.com/guide/simple-mode/) — a PIN-protected, distraction-free menu for children or casual users.

Available when you want it (Tools, pause menu and OSD):
- [On-Screen Display](https://nxredux.com/guide/osd/) reachable anywhere: mute, brightness, motor, Wi-Fi, Bluetooth and LED toggles, screenshot and screen recorder, system monitors, music controls and power off.
- [Music Player](https://nxredux.com/apps/music-player/) with background playback, internet radio and hi-res output on USB DACs, plus a [Media Player](https://nxredux.com/apps/media-player/) with audio and subtitle switching.
- Bundled standalone emulators for [Nintendo DS](https://nxredux.com/emulators/nintendo-ds/), [Nintendo 64](https://nxredux.com/emulators/nintendo-64/) and [Sega Dreamcast](https://nxredux.com/emulators/dreamcast/), all with the system's in-game menu, save states with screenshots, USB-C and Bluetooth audio, and sleep on the power button.
- [PortMaster](https://nxredux.com/apps/portmaster/) and PSP installable on-device from the [Xtras Store](https://nxredux.com/apps/xtras/).
- [Netplay](https://nxredux.com/netplay/) for local wireless multiplayer: press `Y` on a supported game to host or join, with GB and GBA link-cable support, lockstep netplay for the other cores, and N64 and Dreamcast sessions.
- [RetroAchievements](https://nxredux.com/apps/retroachievements/) with full offline support — earn achievements offline, sync later, and browse your whole cached library on the device. Softcore only by design.
- [Cheats](https://nxredux.com/apps/cheats/) downloaded straight from the libretro cheat database, no PC needed.
- [Device Sync](https://nxredux.com/apps/device-sync/) for saves, states, settings and ROMs across devices, [Artwork Manager](https://nxredux.com/apps/artwork-manager/) for box art, and a redesigned [Game Tracker](https://nxredux.com/apps/game-tracker/).
- [Nintendo / Xbox button layout](https://nxredux.com/guide/button-layout/), joystick calibration, and [Developer options](https://nxredux.com/settings/developer/) with SSH, sleep control and debug logging.

Under the hood:
- `nextui.c` split into focused components (game list, game switcher, search, launcher, image loader and more) and `minarch` split into `ma_*` modules, adapted from [carroarmato0's work in NextUI #721](https://github.com/LoveRetro/NextUI/pull/721).
- A reusable UI component library in `common/ui/`, one component per file, wired into every app through a single `ui.mk` fragment.
- Sink-aware audio sample-rate negotiation across music, radio, video and every emulator, with automatic rerouting when a USB-C DAC or Bluetooth device connects — see [Audio settings](https://nxredux.com/settings/audio/).
- clang-format tooling with enforced code style, and per-platform release zips (brick / brickpro / smartpro / smartpros).

Removed or consolidated:
- The `Remove Loading` tweak now happens once in the install script instead of through a resident app.
- The `Battery` history feature is gone — it needed an always-on daemon logging to the SD card, while the status bar already shows the charge level.
- Hardcore RetroAchievements mode is intentionally omitted — NX Redux is not an RA-approved hardcore emulator, so leaving it out keeps your account safe.

## Additional Emulators

Emulators that aren't bundled can be added: PSP through the on-device Xtras store, anything else by copying a community pak into the `/Emus` folder on the SD card. See [Additional Emulators](https://nxredux.com/emulators/additional/).

> ⚠️ Community paks (including the PSP pak) are built for **NextUI**, not for NX Redux. They generally work, but they are not developed, maintained or supported for NX Redux — please don't report NX Redux-specific problems to their developers.

## Upstream

This project is a derivative of [LoveRetro/NextUI](https://github.com/LoveRetro/NextUI), forked while NextUI was licensed under GPL-3.0.

NX Redux now develops independently. NextUI has since moved to the PolyForm Noncommercial 1.0.0 license, which is not compatible with this project's GPL-3.0 — so newer upstream changes are no longer merged, and NX Redux continues from the GPL-3.0 codebase it forked.

## Credits

- [ro8inmorgan](https://github.com/ro8inmorgan), [frysee](https://github.com/frysee) and all contributors for developing NextUI
- [clintonium-119](https://github.com/clintonium-119) for the original [RetroAchievements integration](https://github.com/LoveRetro/NextUI/pull/633) in NextUI that our offline support is built on
- [carroarmato0](https://github.com/carroarmato0) for the [minarch modularization](https://github.com/LoveRetro/NextUI/pull/721) that the `ma_*` split here is based on
- [sinedied](https://github.com/sinedied/perfect-retroshaders) for the shaders that were originally proposed for NextUI in [LoveRetro/NextUI#796](https://github.com/LoveRetro/NextUI/pull/796).
- [KrutzOtrem](https://github.com/KrutzOtrem/Trimui-Brick-Overlays) for the overlays
- [timbueno](https://github.com/timbueno/ArtBookNextUI.theme) for the Artbook theme
- [anthonycaccese](https://github.com/anthonycaccese/art-book-next-es.git) for the Artbook artwork
- [ben16w](https://github.com/ben16w/minui-portmaster) for the Minui-Portmaster

## License

Licensed under **GNU GPL v3.0**, the license NextUI was under at the time of the fork.

All original copyrights are retained.
Modifications in this repository are also distributed under GPL-3.0.

See the [LICENSE](LICENSE) file for details.


> *NX Redux is an independent fork and is not affiliated with the original NextUI project.*
