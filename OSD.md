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

At boot, `launch.sh` overlays our scripts from `$SYSTEM_PATH/osd/widgets/` onto
`/usr/trimui/osd/widgets/`, backing up the stock versions as `<script>.stock`.
Note the wifi/bt toggle scripts write the status file optimistically before the
radio transition completes, so the icon can bounce once via the 3s refresh
during a slow start (~7s for wifi, longer for BT).

Platform note: on **tg5050** the BT toggle powers the adapter off/on via a live
`bluetoothd` (matching the Settings app; killing the daemon wedges other apps'
`bluetoothctl` calls). On **tg5040** it must fully stop the stack — the xradio
combo chip drops WiFi throughput from ~330 KB/s to ~2 KB/s while bluetoothd
runs. The apps tolerate losing bluetoothd mid-call via command timeouts in
`generic_bt.c`.

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
| `toggle_mute` | toggle | Audio mute |
| `toggle_power` | toggle | Power off |
| `static_pic_1` | static | Background/header image |
| `static_battery` | static | Battery level display |
| `static_cpu_freq` | static | CPU frequency display |
| `static_temperature` | static | CPU temperature display |
| `static_dram` | static | RAM usage display |
| `app_music` | app | Music player controls |

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
