# Screen Capture Stack (screenshot, recorder, indicators)

How screenshots and screen recording work, and why they work that way. The
per-device display topology that forces this design is in
[DEVICES.md](DEVICES.md) (§Display topology). OSD widget wiring is in
[OSD.md](OSD.md).

## Components

| Piece | Where | Role |
|---|---|---|
| `screenshot.elf` | `workspace/all/screenshot` | Daemon armed by the OSD toggle; L2+R2 captures a JPEG to `/mnt/SDCARD/Images/Screenshots` |
| `screenrecorder.elf` | `workspace/all/screenrecorder` | Records MP4 to `/mnt/SDCARD/Videos/Recordings` while running |
| `common/drm_scanout.{c,h}` | shared | Standalone DRM plane readback (libc + UAPI only); compiled into both daemons |
| `capture_check()` / mirror publisher | `common/generic_video.c` (included by both platforms' `platform.c`) | Foreground app publishes RGBA frames to a shm mirror while a capture daemon is active |
| Status-bar indicators | `common/api.c` (`PWR_captureStatus`, `ASSET_SCREENSHOT`/`ASSET_RECORD`) | Camera icon when the screenshot daemon is armed, red dot while recording |

Both daemons are PID-file driven: `/tmp/screenshot.pid` and
`/tmp/screenrecorder.pid`, started/stopped by the OSD `toggle_screenshot` /
`toggle_screenrecord` widgets (one shared `set.sh` for all devices).

## Frame sources, in priority order

Every capture path walks the same ladder and **fails honestly** — if no source
is available the screenshot daemon toasts "Capture not available here" and
writes **no file** (a black JPEG with a lying "saved" toast is the failure
mode this replaced).

1. **Live GPU mirror** — `/tmp/fb_mirror.raw` (RGBA), published by the
   foreground app's flip path in `generic_video.c` when a capture PID file
   exists. Vsync'd, exact, but only exists for apps that link the common video
   layer (not third-party paks) — and **only on tg5050**: `capture_check`
   early-returns on every other platform, so no tg5040 app ever publishes a
   mirror.
2. **DRM scanout readback** (`drm_scanout.c`) — opens `/dev/dri/card0` as a
   non-master client (root suffices), enables universal planes, walks planes
   to the active framebuffer, `GETFB2` → `PRIME_HANDLE_TO_FD` → mmap, packs
   rows. Captures *any* app on tg5050, including third-party paks and across
   app switches. On tg5040 `card0` is Mali-only and `GETPLANERESOURCES` fails
   fast, so this source cleanly self-disables there.
3. **disp write-back dump** — tg5040 only, **screenshot daemon only** (the
   ~1s blocking capture disqualifies it as a recorder source). The sunxi
   display engine's write-back channel (`/sys/class/disp/disp/attr/
   capture_dump`) returns the **final composited panel output** — all disp
   layers, including video layers and `trimui_osdd`'s OSD panel and toasts,
   which fb0 structurally cannot see. Since tg5040 has no mirror and no DRM
   readback, this is effectively the primary tg5040 screenshot source; a
   failed attempt latches off for the rest of the retry loop and falls
   through to fbdev. Recipe and kernel gotchas in
   [TESTING.md](TESTING.md#screenshots-for-verification).
4. **fbdev** (`/dev/fb0`) — tg5040 only (`fbdev_usable()` is compile-time
   gated on `-DPLATFORM`, so a tg5050 build can never fall back to fbdev and
   produce guaranteed-black output). The recorder's tg5040 source, and the
   screenshot daemon's fallback when the write-back attr is missing or fails.
   The screenshot daemon additionally samples fb0 content (pread, RGB channel
   masks from `fb_bitfields`) to reject an all-black frame; the recorder
   re-reads `yoffset` per frame to follow panning.

DRM readback details worth keeping: `GETFB2` is ioctl `0xCE` and the
toolchain's UAPI headers predate kernel 5.7, so the struct is `#ifndef`-defined
by hand. Format map: XR24/AR24 → ffmpeg `bgr0`, XB24/AB24 → `rgb0`; skip
framebuffers with a non-zero modifier (AFBC-compressed) and NV12 overlays —
falling through lands on the UI plane. Scanout rows are top-down (no vflip).
Measured on the Smart Pro S: primary plane XR24 1280×720 pitch 5120, linear.

## The mirror liveness protocol (flock, not mtime)

The mirror file caused two classes of bug before the current protocol: stale
mirrors surviving app exit (capture inside a third-party pak encoded nextui's
last frame), and freshness checks based on mtime (mmap stores do **not**
reliably bump file mtime — mtime freshness is a trap).

Current protocol (`generic_video.c` writer, both daemons as readers):

- The writer holds an **advisory `flock(LOCK_EX | LOCK_NB)`** on the mirror fd
  for its whole lifetime. The kernel releases the lock on any process death,
  including SIGKILL — so "writer alive" is exactly "lock held". Readers treat
  the mirror as live only if the flock is still held by someone else.
- The writer's activation-time flock can transiently lose a race against a
  daemon's liveness probe, so `capture_write()` retries the lock until held.
- Geometry travels in a sidecar `/tmp/fb_mirror.info` (`WxH`) — never derived
  from fb ioctls (unreliable on tg5050).
- `PLAT_quitVideo` and capture deactivation unlink both files.
- The recorder must handle the mirror being **recreated**: it compares the
  cached `st_ino` and remaps on inode change (the old code mmap'd the inode
  once and froze on the last frame when the writer restarted).

## Dirty-flag rendering vs capture

nextui (and most paks) render only on activity — no input ⇒ no flips ⇒ the
mirror never publishes. Mitigations:

- `capture_check()` is time-gated (250 ms) off the flip path and stamps the
  last flip time.
- `PLAT_pokeCapture()` (tg5050 only; weak no-op elsewhere, called from
  `PAD_poll` — the single choke point all app main loops pass) re-presents the
  current layer stack every 500 ms while a capture PID file exists, but only
  once the screen has been genuinely idle >1 s, so it never injects a present
  mid-render. On tg5040 the poke isn't needed: fb0 always holds the scanout.
- The recorder waits up to 60 s for a mirror to appear before giving up;
  deactivation also only runs on flips, so an idle app holds the mirror until
  its next redraw after the daemon exits (accepted).

Known niche quirk (tg5050): a capture taken immediately after an idle-arm can
lag the panel by one present — the panel is correct, the captured frame is one
composite behind.

## Recorder encoding: wallclock VFR, not CFR

The A55/A53 cores **cannot sustain 30 fps MJPEG 720p** (measured ~41 ms/frame
at full clocks; paks that cap clocks make it 145 ms/frame), and frame-count
timestamps made videos play ~2× fast. So the recorder feeds ffmpeg with
`-use_wallclock_as_timestamps 1` + `-fps_mode vfr`, and writes fragmented MP4
(`-movflags +frag_keyframe+empty_moov`) so a `kill -9` still leaves a playable
file. Unchanged frames are skipped via memcmp with a 1 s heartbeat — static
screens cost almost nothing. Pixel format is yuvj420p. There is **no usable
hardware encoder**: ffmpeg 6.1 has `h264_v4l2m2m` but the kernel exposes no
`/dev/videoN` (CedarX not wired up). 30 fps is an opportunistic ceiling; the
Brick sustains ~14 fps at 1024×768.

The record toggle onlines cpu2 on tg5050 for encoding headroom, tracked via a
`cpu2_onlined` flag file so it only offlines a core it brought up itself
(the Brick must never get cpu2 offlined — all its cores are online by
default).

## Status-bar indicators

`PWR_captureStatus(&screenshot_armed, &recording)` (common/api.c) reads both
PID files with a 500 ms-TTL cache (`kill(pid, 0)` liveness); `PWR_update` sets
the dirty flag on a state flip and `GFX_blitHardwareGroup` draws the camera
glyph (`ASSET_SCREENSHOT`, THEME_COLOR6) and record dot (`ASSET_RECORD`,
tinted 0xFF453A — `blitSurfaceColor` colormod takes 0xRRGGBB) leftmost of the
status cluster. The glyphs live in all four `assets@Nx.png` sheets — see the
[DEVICES.md](DEVICES.md) rule about pushing all four.

## Daemon lifecycle gotchas

- Both daemons handle SIGHUP but **respect an inherited `SIG_IGN`** — a plain
  handler installed unconditionally overrides the `nohup`/`trap '' HUP`
  disposition and makes the daemon kill itself on adb disconnect (learned the
  hard way). SIGPIPE is ignored in the recorder.
- tg5040 has no `nohup`; test-launch daemons with `trap '' HUP; cmd &`.
- Black-frame signature for verification: a 1280×720 q2 black JPEG is
  exactly 11003 bytes — a suspiciously-sized output is a black capture.
- Capture test injection recipes (L2/R2 are EV_ABS codes 2/5 value 255) are in
  [TESTING.md](TESTING.md).
