# TrimUI OSD (On-Screen Display) — System Overlay

## Overview

`trimui_osdd` is TrimUI's stock on-screen display daemon. It renders a widget-based overlay on top of any running application using hardware display planes — completely independent of the foreground app's rendering pipeline. No pausing, no signal coordination, no SDL conflicts.

Works on **both TG5040 and TG5050**. TG5050 uses DRM planes (`drmModeSetPlane`), TG5040 uses the sunxi display engine (`/dev/disp` ioctl with hardware channels/layers).

## Architecture

```
┌─────────────────────────────────────────┐
│  DRM Display Pipeline (hardware)        │
│                                         │
│  Plane 0: Foreground app (nextui/emu)   │  ← SDL/EGL renders here
│  Plane 1: OSD overlay (trimui_osdd)     │  ← DRM dumb buffer, composited by hardware
│                                         │
│  GPU composites both planes per frame   │
└─────────────────────────────────────────┘
```

The OSD renders to a separate DRM plane via `drmModeSetPlane()`. The GPU hardware composites this plane on top of the foreground app's plane automatically. No interference, no tearing, no coordination needed.

## Binary Location

```
/usr/trimui/osd/trimui_osdd
```

The binary is model-specific: the Brick, Brick Pro and Smart Pro firmwares each
ship a different `trimui_osdd` build, and they are not interchangeable. The main
OSD assets are also resolution-locked (`bg.png` and the `block*.png` tiles:
1024×768/124px grid on Brick and Brick Pro vs 1280×720/120px grid on Smart Pro).

The skeleton therefore keeps one layered source tree at `skeleton/SYSTEM/osd/`
rather than a copy per device:

| Layer | Holds |
|---|---|
| `common/` | What every device shares — widgets, icons, progress art, `key.wav` |
| `res/<WxH>/` | Anything with a pixel coordinate baked in — `block*.png`, and the `show_*.sh` toast scripts, which differ only in their baked-in coordinates (13 of 14 in `"x"` alone, e.g. 650 vs 850; `show_volume_msg.sh` in both `"x"` and `"y"`) |
| `device/<dev>/` | `trimui_osdd`, `osdlayout.json`, `bg.png`, plus any file that device overrides |

`scripts/assemble-osd.sh` composes those layers during `make package` into the
shape each platform's `launch.sh` reads: tg5040 gets `osd/` (common) plus
`osd-$DEVICE/` (res + device), tg5050 gets a single flat `osd/`. Each device zip
carries only its own assets.

At boot `launch.sh` copies `osd/` and then, on tg5040, overlays `osd-$DEVICE/`,
gated by a content-hash stamp (`/usr/trimui/osd/.nx_osd_stamp`) so the rootfs is
only written when the SD tree actually changed. If no overlay exists for the
running model, the sync is skipped entirely and the firmware's own
`/usr/trimui/osd` is left alone — a partial tree (shared widgets, no daemon, no
`bg.png`) would leave the OSD dead rather than merely un-themed.

The Smart Pro binary and assets were extracted from
`sd_recovery_tg5040_smartpro_ver1.1.1_20251128.img` (stock rootfs, ext4 at
sector 126478). The Brick Pro's came from
`sd_recovery_tg4040_brickpro_ver1.1.1_20260717.img` (ext4 at sector 126432 —
the image holds three 540 MB ext4 filesystems; only the first has a populated
`/usr/trimui`). Only three files actually differ between the two 1024×768
models: `trimui_osdd`, `bg.png` and `osdlayout.json`; the block tiles and every toast
script are byte-identical. Brick Pro's stock
layout includes the battery widget, so `device/brickpro/osdlayout.json` keeps it
(the widget itself lives in the shared `common/widgets/static_battery`, and is
simply not referenced by the Brick or Smart Pro layouts). Extraction recipe,
run from a Linux container since macOS cannot mount ext4:

```bash
dd if=<recovery>.img of=/tmp/fs.img bs=512 skip=126432 count=1105920
debugfs -R "rdump /usr/trimui/osd /out" /tmp/fs.img
```

## Dependencies

### Shared Libraries

| Library | Location | Purpose |
|---------|----------|---------|
| `libshmvar.so` | `/usr/trimui/lib/` | Shared memory variables (inter-process state) |
| `libdrm.so.2` | system | DRM/KMS display plane management |
| `libSDL2` | system | Joystick input (SDL_JoystickOpen, SDL_WaitEvent) |

### Services

| Service | Purpose | Required |
|---------|---------|----------|
| `trimui_inputd` | GPIO input daemon, provides `/dev/input/event*` joystick | Yes (for input) |

### Filesystem

| Path | Purpose |
|------|---------|
| `/usr/trimui/osd/` | Working directory (must `cd` here before launching) |
| `/usr/trimui/osd/osdlayout.json` | Widget layout configuration |
| `/usr/trimui/osd/widgets/` | Widget definitions (config.json, set.sh, refresh.sh per widget) |
| `/usr/trimui/osd/regular.ttf` | Font for OSD text |
| `/usr/trimui/osd/*.png` | UI assets (backgrounds, icons, progress bars) |
| `/tmp/trimui_osd/` | Runtime IPC directory (created by daemon) |

## Starting the Daemon

**Critical**: Must be launched from its own directory.

```bash
cd /usr/trimui/osd && ./trimui_osdd &
```

The daemon requires `LD_LIBRARY_PATH` to include `/usr/trimui/lib/` (for `libshmvar.so`). Our `launch.sh` already sets this via:

```bash
export LD_LIBRARY_PATH=$SYSTEM_PATH/lib:$SHARED_SYSTEM_PATH/lib:/usr/trimui/lib:$LD_LIBRARY_PATH
```

In our launch.sh (TG5050 only):

```bash
# Start TrimUI OSD overlay daemon (system-wide quick menu)
cd /usr/trimui/osd && ./trimui_osdd &
cd "$SYSTEM_PATH/bin"
```

## IPC (Inter-Process Communication)

### Trigger Files

| File | Action |
|------|--------|
| `touch /tmp/show_osdd` | Show the OSD overlay |
| `touch /tmp/hide_osdd` | Hide the OSD overlay |

### State File

| File | Meaning |
|------|---------|
| `/tmp/trimui_osd/osdd_show_up` | Exists while OSD is visible, removed when hidden |

### Toast Messages

Write JSON to `/tmp/trimui_osd/osd_toast_msg` to show temporary notifications:

```json
{
    "type": "volume",
    "id": "com.trimui.osd.msg.volumeglobal",
    "duration": 1000,
    "size": 0,
    "x": 850,
    "y": 30,
    "w": 300,
    "h": 80,
    "message": "8 / 20",
    "font": "",
    "bg": "",
    "icon": "",
    "fontsize": 24,
    "fontcolor": "FFFFFFFF"
}
```

See `/usr/trimui/osd/show_*.sh` scripts for examples.

### Widget IPC Directories

Each widget has a directory under `/tmp/trimui_osd/`:

```
/tmp/trimui_osd/
├── hotkeyshow              # Contains hotkey name (e.g., "HOME")
├── osdd_show_up            # Exists when OSD is visible
├── slider_backlight/
├── slider_volume/
├── stepper_fanlevel/
├── toggle_bt/
├── toggle_cpu/
├── toggle_led/
├── toggle_rumble/
├── toggle_temperature/
└── toggle_wifi/
```

## Widget Configuration

### osdlayout.json

```json
{
    "hotkey": "MENU+SELECT",
    "timefadein": 250,
    "timefadeout": 0,
    "timeupdatevalue": 3000,
    "widgetlist": [
        {"package": "com.trimui.osd.static.pic1"},
        {"package": "com.trimui.osd.app.musicplayer"},
        {"package": "com.trimui.osd.slider.backlight"},
        {"package": "com.trimui.osd.slider.volumeglobal"},
        {"package": "com.trimui.osd.toggle.btenable"},
        {"package": "com.trimui.osd.toggle.wifienable"},
        {"package": "com.trimui.osd.toggle.ledenable"},
        {"package": "com.trimui.osd.toggle.rumble"},
        {"package": "com.trimui.osd.static.temperature"},
        {"package": "com.trimui.osd.static.cpuinfo"},
        {"package": "com.trimui.osd.stepper.fanlevel"}
    ]
}
```

Note: the `hotkey` field is **inert** — the daemon never reads it (the string
"hotkey" doesn't appear in the binary). The OSD is actually triggered by our
keymon via `/tmp/show_osdd`: HOME press (instant) or MENU long-press (500ms).
Verified empirically: a short MENU+SELECT chord does not open the OSD.

### Widget Directory Structure

Each widget under `/usr/trimui/osd/widgets/` has:

```
widgets/toggle_wifi/
├── config.json       # Widget metadata (name, type, icons, script names, status file)
├── set.sh            # config.json "launch": called on press (toggles get an argument)
└── update.sh         # config.json "update": refreshes the status file (every 3s)
```

The script names come from `config.json` — toggles use `update.sh`, the static
widgets (temperature, CPU clock) use `refresh.sh`. The daemon reads the widget's
current value from the `status` file path in `config.json`.

Both platforms vendor the **complete OSD tree** on the SD card — daemon binary,
UI assets, toast scripts, and every widget (stock and custom) — assembled per
device from `skeleton/SYSTEM/osd/` at package time. At boot, `launch.sh` copies
the whole tree (~1MB) over `/usr/trimui/osd/`, making the SD card the source of
truth regardless of firmware version. `regular.ttf` (16MB CJK font) is
deliberately not shipped — the firmware's copy is used, since files are only
overwritten, never deleted. `trimui_osdd` has `/usr/trimui/osd/` hardcoded in
the closed-source binary, so it can't read from the SD card directly — the
boot-time copy is the only way to control it. Every model's daemon build differs
(different md5), so each lives in its own `device/<dev>/` layer.

Activating screenshot, screen record, or power auto-hides the OSD panel
(`touch /tmp/hide_osdd`) so the user lands back on the app; toasts still
render while the panel is hidden.
Note the wifi/bt toggle scripts write the status file optimistically before the
radio transition completes, so the icon can bounce once via the 3s refresh
during a slow start (~7s for wifi, longer for BT).

Platform note: on **tg5050** the BT toggle powers the adapter off/on via a live
`bluetoothd` (matching the Settings app; killing the daemon wedges other apps'
`bluetoothctl` calls). On **tg5040** it must fully stop the stack — the xradio
combo chip drops WiFi throughput from ~330 KB/s to ~2 KB/s while bluetoothd
runs. The apps tolerate losing bluetoothd mid-call via command timeouts in
`generic_bt.c`.

Capture toggles: both are PID-file driven. `toggle_screenshot` starts/stops
`screenshot.elf` (daemon owns `/tmp/screenshot.pid`; L2+R2 together captures a
frame — the widget shows a hint toast on enable, and the daemon toasts
"Screenshot saved" after each capture).
`toggle_screenrecord` on tg5050 brings cpu2 online and starts
`screenrecorder.elf <output> 1280 720` (owns `/tmp/screenrecorder.pid`); on
tg5040 it records `/dev/fb0` directly with ffmpeg and the script owns the PID
file. The foreground app (`capture_check()` in `generic_video.c`, tg5050 only)
notices the PID files on rendered frames and publishes RGBA frames to the
`/tmp/fb_mirror.raw` shm. Because dirty-flag apps like nextui only render on
activity, capture starts once the user interacts after toggling; the recorder
waits up to 60s for the mirror before giving up.

### Available Widgets

| Widget | Type | Purpose |
|--------|------|---------|
| `slider_backlight` | slider | Screen brightness |
| `slider_volume_global` | slider | System volume |
| `stepper_fan_level` | stepper | Fan speed control |
| `toggle_bt` | toggle | Bluetooth on/off |
| `toggle_wifi` | toggle | WiFi on/off |
| `toggle_led` | toggle | LED strip on/off |
| `toggle_rumble` | toggle | Vibration on/off |
| `toggle_mute` | toggle | Audio mute (osdctl-based) |
| `toggle_power` | toggle | Power off — stock ships an empty `set.sh`; ours implements the launch.sh contract (`rm /tmp/nextui_exec` + `touch /tmp/poweroff` + kill foreground app → `poweroff_next`). In-game press exits minarch without quicksave. |
| `toggle_screenshot` | toggle | Screenshot daemon on/off (custom; L2+R2 captures to `/mnt/SDCARD/Images/Screenshots`) |
| `toggle_screenrecord` | toggle | Screen recording on/off (custom; records to `/mnt/SDCARD/Videos/Recordings`) |
| `static_pic_1` | static | Background/header image (not shipped — removed from the tg5040 vendored tree) |
| `static_battery` | static | Battery level display |
| `static_cpu_freq` | static | CPU frequency display |
| `static_temperature` | static | CPU temperature display |
| `static_dram` | static | Used-memory display — stock widget was incomplete (empty `set.sh`, no update hook); ours adds `refresh.sh` (MemTotal−MemAvailable) |
| `app_music` | app | Music player controls — vendored but not in the layout yet; to be integrated with `workspace/all/musicplayer` |

## DRM Plane Details

The daemon creates 4 DRM dumb buffers (1280x720 ARGB8888, ~3.6MB each):

| Buffer | Purpose |
|--------|---------|
| `osd` | Main OSD overlay content |
| `msg` | Toast message layer |
| `blank` | Blank/transparent buffer for hiding |
| `ui` | UI elements layer |

Plane control via sysfs:

```
/sys/class/de_plane/de_plane/attr/rmpos
```

Shows active planes:

```
blender@  281000: enable ( ae81000)
     0   |   true |    0    |  false  | 1280x 720+   0+   0
     1   |   true |    3    |   true  | 1280x 720+   0+   0
     2   |   true |    4    |   true  | 1280x 720+   0+   0
```

## Input Handling

The daemon grabs `/dev/input/event4` (joystick) exclusively while the OSD is visible:

```
grab link:[/proc/PID/fd/8] -> [/dev/input/event4]
grab ret=0
```

This means the foreground app does NOT receive joystick input while the OSD is active — the OSD has exclusive control. When dismissed, the grab is released.

It also attempts to signal `trimui_inputd` via `/tmp/trimui_inputd/grab` but this may not exist in our setup (non-critical).

## Integration with NextUI-Redux

Both platforms use the same approach: `keymon` detects the trigger → toggles OSD via flag files.

### TG5050

- **keymon** detects Home button (`CODE_HOME=172`) and Menu long-press (`CODE_MENU2=316`, 500ms threshold)
- On trigger: checks `/tmp/trimui_osd/osdd_show_up` to toggle show/hide
- Menu tap opens the context menu in nextui

### TG5040

- **keymon** detects Menu long-press (`CODE_MENU2=316`, 500ms threshold) — no Home button on this device
- On trigger: same toggle logic via `/tmp/trimui_osd/osdd_show_up`
- Menu tap opens the context menu in nextui

### nextui

- `PAD_quickMenuPressed()` returns 0 on all platforms — built-in quick menu disabled
- OSD is handled entirely by keymon + trimui_osdd

## Known Issues

- `trimui_inputd` grab signaling (`/tmp/trimui_inputd/grab`) may not work if `trimui_inputd` isn't configured for it

Previously listed issues, since fixed:

- ~~Missing `set.sh` for `static_temperature`/`static_cpu_freq`~~ — no-op stubs shipped in the skeleton
- ~~Fan level widget references `/tmp/system/set_fanlevel`~~ — replaced with an `osdctl`-based script driving NextUI's fan control
- ~~WiFi/BT toggles not reflected in Settings, hangs when toggling while Settings open~~ — apps now read live radio state (sysfs/HCI ioctl) instead of cached config, `bluetoothctl` shell-outs are timeout-killed, and `CFG_sync()` re-captures radio state so it can't clobber the OSD's `minuisettings.txt` edits

## Future Work

- Potentially build our own OSD daemon using the same DRM plane approach for full control
