# PPSSPP v1.20.1 Build for TG5040

Cross-compiled PPSSPP PSP emulator for TG5040 (PowerVR GE8300, Cortex-A53 quad-core).

## Quick Start

```bash
# From project root:

# 1. Clone and patch source (one-time)
docker run --rm \
  -v "$(pwd)/workspace":/root/workspace \
  -v "$(pwd)/skeleton/EXTRAS/Emus/tg5040/PSP.pak/src":/root/build \
  -w /root/build \
  ghcr.io/loveretro/tg5040-toolchain:latest make clone

# 2. Build ffmpeg (one-time, GCC 8.3 compatibility)
docker run --rm \
  -v "$(pwd)/workspace":/root/workspace \
  -v "$(pwd)/skeleton/EXTRAS/Emus/tg5040/PSP.pak/src":/root/build \
  -w /root/build \
  ghcr.io/loveretro/tg5040-toolchain:latest make ffmpeg

# 3. Build PPSSPP
docker run --rm \
  -v "$(pwd)/workspace":/root/workspace \
  -v "$(pwd)/skeleton/EXTRAS/Emus/tg5040/PSP.pak/src":/root/build \
  -w /root/build \
  ghcr.io/loveretro/tg5040-toolchain:latest make build

# 4. Deploy binary + assets to PSP.pak
make deploy
```

Output: `ppsspp/build/PPSSPPSDL_stripped` (~19MB)

## Directory Structure

```
PSP.pak/
├── src/
│   ├── Makefile                    # Build orchestration
│   ├── toolchain-aarch64.cmake     # Cross-compilation toolchain
│   ├── ppsspp.patch                # Source patches (GCC 8.3 + PowerVR fixes)
│   ├── pvr_libs/                   # PowerVR GPU libraries (from device)
│   └── ppsspp/                     # Cloned source (not committed)
├── PPSSPPSDL                       # Built binary (stripped)
├── launch.sh                       # Device launch script
├── default-brick.ini               # Trimui Brick defaults
├── default-smartpro.ini            # Trimui Smart Pro defaults
└── assets/                         # PPSSPP runtime assets
```

## Source Patches (ppsspp.patch)

PPSSPP commit: `eb859735feddf88dbe651763f366a7705612113a`

### GCC 8.3 Compatibility

| File | Fix |
|------|-----|
| `Core/Screenshot.cpp` | CTAD workaround — explicit `IndependentTask<decltype(task)>` |
| `ffmpeg/linux_nextui_arm64.sh` | Rebuild FFmpeg with GCC 8.3 (pre-built uses GCC 10+ outline atomics) |

### PowerVR GE8300 GPU Support

| File | Fix |
|------|-----|
| `CMakeLists.txt` | `NATIVE_GLES3` option + PVR libs linking |
| `Common/GPU/OpenGL/GLCommon.h` | Conditional `<GLES3/gl3.h>` vs `<GLES2/gl2.h>` includes |
| `Common/GPU/OpenGL/gl3stub.h` | Core GLES3 declarations wrapped with `#ifndef NATIVE_GLES3` |
| `Common/GPU/OpenGL/gl3stub.c` | Core GLES3 loading skipped in NATIVE_GLES3 mode; extension-only |
| `Common/GPU/OpenGL/GLFeatures.cpp` | Guard `glGetStringi` to require actual GLES3 init |
| `Common/GPU/OpenGL/thin3d_gl.cpp` | NULL check on `glCopyImageSubDataOES` before setting `framebufferCopySupported` |
| `Common/GPU/OpenGL/GLQueueRunner.cpp` | NULL guard before calling `glCopyImageSubDataOES` in PerformCopy |
| `Common/GPU/thin3d.cpp` | Force `col.a = 1.0` in presentation shaders (fbdev alpha fix) |
| `SDL/SDLGLGraphicsContext.cpp` | `SDL_GL_ALPHA_SIZE=0` hint + GL version logging |
| `Common/ExceptionHandlerSetup.cpp` | Crash diagnostics (registers, memory maps) |

## Bugs Fixed

### 1. gl3stub Symbol Interposition

PPSSPP's `gl3stub.c` defines GLES3 functions as global function pointer variables, which
shadow the real `libGLESv2.so` symbols on GLES 3.2 devices. Fixed with `NATIVE_GLES3`:
core GLES3 functions come from real headers, gl3stub only handles extension pointers.

### 2. Tekken 6 Crash (NULL glCopyImageSubDataOES)

`GLQueueRunner::PerformCopy()` called `glCopyImageSubDataOES()` which was NULL — PowerVR
GE8300 has no copy_image extension. Fixed with NULL checks in both `thin3d_gl.cpp` and
`GLQueueRunner.cpp`.

### 3. FFmpeg Link Error (Outline Atomics)

Pre-built FFmpeg libs used GCC 10+ outline atomics (`__aarch64_cas8_acq_rel`). Fixed by
rebuilding FFmpeg from source with GCC 8.3 via `linux_nextui_arm64.sh`.

### 4. Tekken 6 Black Screen on Menus/Videos (PowerVR fbdev Alpha)

2D content (menus, MPEG videos) rendered correctly to the GL backbuffer but appeared black
on screen. Root cause: **PowerVR's fbdev display compositor uses the backbuffer's alpha
channel for transparency**. PSP games using GE_FORMAT_8888 (RGBA8888) framebuffers often
write alpha=0 for 2D content, making the output invisible. Games using GE_FORMAT_5551
(1-bit alpha, usually 1) were unaffected — explaining why FIFA Street 2 worked but Tekken 6
menus did not.

**Diagnosis**: `glReadPixels` before `SDL_GL_SwapWindow` confirmed correct RGB values but
alpha=0 for all frames. Masking alpha writes (`colorMask 0x7`) made everything black,
confirming the compositor depends on alpha.

**Fix**: Force `col.a = 1.0` in the presentation fragment shaders (`Common/GPU/thin3d.cpp`)
so the final screen output is always fully opaque. Also set `SDL_GL_ALPHA_SIZE=0` as a hint
(ignored by PowerVR but harmless).

## PVR Libraries

PowerVR GPU libraries needed at link time (pull from device):

```bash
mkdir -p pvr_libs
adb pull /usr/lib/libIMGegl.so pvr_libs/
adb pull /usr/lib/libglslcompiler.so pvr_libs/
adb pull /usr/lib/libsrv_um.so pvr_libs/
adb pull /usr/lib/libusc.so pvr_libs/
```

## GPU Info

- **GPU**: PowerVR Rogue GE8300
- **GL**: OpenGL ES 3.2 build 1.19@6345021 (111 extensions)
- **No Vulkan**: BSP has no libvulkan.so
- **No copy_image**: OES/NV/EXT/ARB all absent
