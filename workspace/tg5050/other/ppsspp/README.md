# PPSSPP Standalone Emulator Build

Cross-compiled PPSSPP v1.20.1 for TG5050 (Mali-G57) with custom in-game overlay menu.

## Prerequisites

- Docker with TrimUI toolchain image:
  - `ghcr.io/loveretro/tg5050-toolchain:latest` (GCC 10.3)

> **Note:** The tg5050 toolchain is missing GLES3/EGL/KHR headers. These are provided
> from `workspace/all/include/` (copied from the tg5040 toolchain).

## Source Setup

Handled by the platform makefile (`workspace/tg5050/makefile`):

```bash
cd workspace/tg5050
make other/ppsspp/ppsspp
```

This clones the PPSSPP repo at the pinned commit, initializes submodules, and applies
`ppsspp.patch` and `ppsspp-ffmpeg.patch`.

## Build

```bash
docker run --rm \
  -v "$(pwd)/workspace":/root/workspace \
  -w /root/workspace/tg5050/other/ppsspp \
  ghcr.io/loveretro/tg5050-toolchain:latest make build
```

First build requires ffmpeg:

```bash
docker run --rm \
  -v "$(pwd)/workspace":/root/workspace \
  -w /root/workspace/tg5050/other/ppsspp \
  ghcr.io/loveretro/tg5050-toolchain:latest bash -c "make ffmpeg && make build"
```

### Key build flags

| Flag | Value | Reason |
|------|-------|--------|
| `USING_GLES2` | ON | Build for OpenGL ES (not desktop GL) |
| `USING_EGL` | ON | TG5050 uses EGL for GL context management |
| `USING_FBDEV` | ON | Framebuffer device support |
| `VULKAN` | OFF | Not using Vulkan |
| `EMU_OVERLAY` | ON | NextUI in-game settings overlay |

## Output & Deployment

Build output: `ppsspp/build/PPSSPPSDL_stripped`

Deploy to skeleton (run from project root):

```
skeleton/EXTRAS/Emus/tg5050/PSP.pak/PPSSPPSDL   <- ppsspp/build/PPSSPPSDL_stripped
skeleton/EXTRAS/Emus/shared/ppsspp/assets/       <- ppsspp/assets/
```

```bash
cp workspace/tg5050/other/ppsspp/ppsspp/build/PPSSPPSDL_stripped \
   skeleton/EXTRAS/Emus/tg5050/PSP.pak/PPSSPPSDL
```

## Source Patches

- `ppsspp.patch` — Applied from ppsspp root: EGL globals, overlay integration, crash
  diagnostics (CMakeLists.txt, SDLMain.cpp, SDLJoystick.cpp, SDLOverlay.cpp/h)
- `ppsspp-ffmpeg.patch` — Applied from ppsspp/ffmpeg submodule: `linux_nextui_arm64.sh`
  cross-build script

## Overlay Integration

The overlay hooks into `SDL/SDLMain.cpp` via `SDL/SDLOverlay.cpp`. When the menu button
(SDL button 8) is pressed during gameplay:

1. Emulation pauses via `Core_Break()`
2. Overlay menu renders using the shared `emu_overlay` library
3. Settings changes are written to `ppsspp.ini` and `g_Config.Reload()` is called
4. Save/load state uses PPSSPP's `SaveState::SaveSlot()`/`LoadSlot()`
5. Emulation resumes via `Core_Resume()`

## Build Notes

**macOS case-insensitive filesystem**: The overlay common dir (`workspace/all/common/`)
contains `sdl.h` which conflicts with `SDL.h` on macOS Docker volume mounts. The
CMakeLists.txt uses `set_source_files_properties()` to scope the overlay include path
to overlay source files only, and `SDLOverlay.h` uses `<SDL2/SDL.h>` instead of `"SDL.h"`.

**GLES3 gl3stub symbol conflict**: PPSSPP's `Common/GPU/OpenGL/gl3stub.c` redefines
GLES3 functions as global function pointer variables loaded via `eglGetProcAddress`.
The overlay works around this by loading VAO functions directly via
`SDL_GL_GetProcAddress()` into its own `pfn_` prefixed function pointers in
`emu_overlay_sdl.c`.

**GL context management**: `SDLOverlay.cpp` captures the GL context during `Overlay_Init()`
and calls `SDL_GL_MakeCurrent()` before overlay GL operations.

**SDL game controller event watcher bypass**: Fixed by adding `isOverlayMenuButton()` check
in `SDLJoystick::ProcessInput()` that uses `SDL_GameControllerGetBindForButton()` to detect
and skip any game controller button bound to raw button 8.

## GPU: Mali-G57 (TG5050)

- OpenGL ES 3.2
- EGL-based GL context
- Vulkan 1.3.296 available (not currently used)

## Modified PPSSPP Source Files

- `CMakeLists.txt` — EMU_OVERLAY integration
- `Common/ExceptionHandlerSetup.cpp` — Crash diagnostics with register dump
- `Common/GPU/OpenGL/GLQueueRunner.cpp` — Null check for glCopyImageSubDataOES
- `Common/GPU/OpenGL/thin3d_gl.cpp` — framebufferCopySupported safety check
- `SDL/SDLGLGraphicsContext.cpp` — Made EGL globals non-static for overlay buffer swap
- `SDL/SDLMain.cpp` — Overlay menu check in main loop; filter raw button 8 events
- `SDL/SDLJoystick.cpp` — Filter game controller events for button 8 via `isOverlayMenuButton()`
- `SDL/SDLOverlay.cpp` — Overlay integration logic (EGL swap path)
- `SDL/SDLOverlay.h` — Overlay public API
- `ffmpeg/linux_nextui_arm64.sh` — FFmpeg cross-build script
