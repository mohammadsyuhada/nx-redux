# Developer Guide

NX-Redux is built with Docker cross-toolchains for two device platforms
(`tg5040`, `tg5050`) plus a native desktop debug build, and is developed
against real TrimUI hardware over adb. The developer documentation lives in
[`.dev/`](.dev/), organized by topic:

| Doc | Covers |
|---|---|
| [.dev/BUILD.md](.dev/BUILD.md) | Build targets, desktop setup, full & per-component builds, deploy, IDE/clangd, formatting, build gotchas |
| [.dev/DEVICES.md](.dev/DEVICES.md) | Per-device hardware facts: platforms, panels/scaling, display topology, CPU tables, firmware minimums, filesystem & shell quirks |
| [.dev/ARCHITECTURE.md](.dev/ARCHITECTURE.md) | Launch loop, dirty-flag rendering, config system, UI conventions, storage formats, RetroAchievements |
| [.dev/TESTING.md](.dev/TESTING.md) | adb deploy rules, headless launch & input injection, screenshots, CPU profiling |
| [.dev/CAPTURE.md](.dev/CAPTURE.md) | Screenshot/screen-recorder stack: DRM readback, GPU mirror protocol, VFR recording, status indicators |
| [.dev/AUDIO.md](.dev/AUDIO.md) | Output routing (audiomon), rate negotiation, resampling traps, libmsettings divergence |
| [.dev/OSD.md](.dev/OSD.md) | The `trimui_osdd` overlay: architecture, layered skeleton source, overlay mount, widgets, IPC |
| [.dev/PAKS.md](.dev/PAKS.md) | Making emulator/tool paks, plus NX-Redux pak resolution and the Xtras catalog contract |
| [.dev/INPUT_MAPPING.md](.dev/INPUT_MAPPING.md) | Per-device button/axis/event-node mappings |
| [.dev/DEV_TODO.md](.dev/DEV_TODO.md) | Planned work, not yet built |
| [.dev/DEV_CHECKLIST.md](.dev/DEV_CHECKLIST.md) | Built work awaiting hardware verification |
