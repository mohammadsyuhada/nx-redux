# OSD Widget NextUI Integration (TG5050) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make TG5050 OSD widgets (volume, brightness, LED, WiFi, BT, rumble, fan) actually control hardware by bridging them to NextUI's `libmsettings` shared memory and platform APIs.

**Architecture:** Build a CLI tool `osdctl` that links `libmsettings` and exposes get/set operations to shell. Rewrite widget `set.sh`/`update.sh`/`refresh.sh` scripts to use `osdctl` instead of TrimUI's `shmvar` + `/tmp/system/` flag files. For WiFi/BT, call the actual init scripts. For LED/rumble, write to sysfs directly.

**Tech Stack:** C (cross-compiled with tg5050-toolchain), shell scripts, libmsettings, tinyalsa, sysfs

---

## File Structure

### New files
- `workspace/all/osdctl/osdctl.c` — CLI tool source (links libmsettings)
- `workspace/all/osdctl/Makefile` — build config (follows syncsettings pattern)

### Modified files (TG5050 widget scripts)
- `skeleton/SYSTEM/tg5050/osd/widgets/slider_volume_global/set.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/slider_backlight/set.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/stepper_fan_level/set.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/toggle_wifi/set.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/toggle_wifi/update.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/toggle_bt/set.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/toggle_bt/update.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/toggle_led/set.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/toggle_led/update.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/toggle_rumble/set.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/toggle_rumble/update.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/toggle_mute/set.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/static_cpu_freq/set.sh`
- `skeleton/SYSTEM/tg5050/osd/widgets/static_temperature/set.sh`

### Deployment
- `osdctl.elf` binary goes to `skeleton/SYSTEM/tg5050/bin/osdctl` (alongside `shmvar`, `fancontrol`, etc.)
- `libmsettings.so` must be deployed to `skeleton/SYSTEM/tg5050/lib/libmsettings.so` (required by `osdctl` at runtime)

### Known Limitations
- Rumble toggle state is stored in `/tmp/` (volatile) — lost on reboot. No `rumble` field exists in `SettingsV1`, so persistent storage would require a settings migration (out of scope).

---

## Key Reference: How NextUI Controls Hardware

### Volume (0-20)
- **Shared memory:** `settings->speaker` (or `settings->headphones` if jack/BT)
- **Hardware:** `SetRawVolume()` → tinyalsa mixer `"DAC Volume"` on audiocodec card, percentage = `value * 5`
- **Speaker mute:** `/sys/class/speaker/mute` (1 = muted when volume is 0)

### Brightness (0-10)
- **Shared memory:** `settings->brightness`
- **Hardware:** `SetRawBrightness()` → `/sys/class/backlight/backlight0/brightness`, raw = `10 + 21 * value` (clamped 10-220)

### Fan Speed (-2 default, -1 quiet, -3 performance, 0-100 manual)
- **Shared memory:** `settings->fanSpeed`
- **Hardware:** `SetRawFanSpeed()` → kills existing `fancontrol`, launches `$SYSTEM_PATH/bin/fancontrol <mode>`

### WiFi
- **Config file:** `$SHARED_USERDATA_PATH/minuisettings.txt` → `wifi=0/1`
- **Enable:** `$SYSTEM_PATH/etc/wifi/wifi_init.sh start`
- **Disable:** `$SYSTEM_PATH/etc/wifi/wifi_init.sh stop`
- **Check status:** `wpa_cli -p /etc/wifi/sockets status` or check interface exists

### Bluetooth
- **Config file:** `$SHARED_USERDATA_PATH/minuisettings.txt` → `bluetooth=0/1`
- **Enable:** `$SYSTEM_PATH/etc/bluetooth/bt_init.sh start`
- **Disable:** `$SYSTEM_PATH/etc/bluetooth/bt_init.sh stop`
- **Check status:** `hcitool dev | grep -q hci0`

### LED
- **On/Off:** Three sysfs paths must all be written:
  - `/sys/class/led_anim/max_scale` — main scale
  - `/sys/class/led_anim/max_scale_lr` — left/right LEDs
  - `/sys/class/led_anim/max_scale_f1f2` — function button LEDs
- Write 0 to turn off, write brightness value (e.g. 255) to turn on
- **No persistent state in libmsettings** — LED settings are in `config.c` `LightSettings` structure

### Rumble
- **On/Off:** `/sys/class/gpio/gpio236/value` — write 1 for on, 0 for off
- **Level:** `/sys/class/motor/level` — write strength value

### Shared Memory Layout
- POSIX shm at `/dev/shm/SharedSettings`
- Binary struct `SettingsV1` (see `workspace/tg5050/libmsettings/msettings.c:19-48`)
- Key: `shm_open("/SharedSettings", ...)`

---

## Tasks

### Task 1: Build `osdctl` CLI tool

**Files:**
- Create: `workspace/all/osdctl/osdctl.c`
- Create: `workspace/all/osdctl/Makefile`

- [ ] **Step 1: Create osdctl.c**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "msettings.h"

// Fallback paths if env vars are not set (OSD daemon may not inherit NextUI's env)
#define DEFAULT_USERDATA_PATH "/mnt/SDCARD/.userdata/tg5050"
#define DEFAULT_SHARED_USERDATA_PATH "/mnt/SDCARD/.userdata/shared"

static void ensure_env(void) {
    if (!getenv("USERDATA_PATH"))
        setenv("USERDATA_PATH", DEFAULT_USERDATA_PATH, 1);
    if (!getenv("SHARED_USERDATA_PATH"))
        setenv("SHARED_USERDATA_PATH", DEFAULT_SHARED_USERDATA_PATH, 1);
}

static void usage(void) {
    fprintf(stderr, "Usage: osdctl get <property>\n"
                    "       osdctl set <property> <value>\n"
                    "\n"
                    "Properties:\n"
                    "  volume      (0-20)\n"
                    "  brightness  (0-10)\n"
                    "  fanspeed    (-3 to 100)\n"
                    "  mute        (0-1)\n");
    exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 3)
        usage();

    ensure_env();
    InitSettings();

    const char* cmd = argv[1];
    const char* prop = argv[2];

    if (strcmp(cmd, "get") == 0) {
        if (strcmp(prop, "volume") == 0)
            printf("%d\n", GetVolume());
        else if (strcmp(prop, "brightness") == 0)
            printf("%d\n", GetBrightness());
        else if (strcmp(prop, "fanspeed") == 0)
            printf("%d\n", GetFanSpeed());
        else if (strcmp(prop, "mute") == 0)
            printf("%d\n", GetMute());
        else
            usage();
    } else if (strcmp(cmd, "set") == 0) {
        if (argc < 4)
            usage();
        int value = atoi(argv[3]);

        if (strcmp(prop, "volume") == 0)
            SetVolume(value);
        else if (strcmp(prop, "brightness") == 0)
            SetBrightness(value);
        else if (strcmp(prop, "fanspeed") == 0)
            SetFanSpeed(value);
        else if (strcmp(prop, "mute") == 0)
            SetMute(value);
        else
            usage();
    } else {
        usage();
    }

    QuitSettings();
    return 0;
}
```

- [ ] **Step 2: Create Makefile**

Follow `workspace/all/syncsettings/Makefile` pattern exactly:

```makefile
###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
	$(error please specify PLATFORM, eg. PLATFORM=trimui make)
endif

ifeq (,$(CROSS_COMPILE))
	$(error missing CROSS_COMPILE for this toolchain)
endif

###########################################################

TARGET = osdctl
SOURCE = $(TARGET).c

CC = $(CROSS_COMPILE)gcc
CFLAGS   = $(OPT) -I$(PREFIX_LOCAL)/include -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\"
LDFLAGS	 = -Os -L$(PREFIX_LOCAL)/lib -L../../$(PLATFORM)/libmsettings -lmsettings -lrt -ldl -Wl,--gc-sections -s

PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all: $(PREFIX_LOCAL)/include/msettings.h
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -f $(PRODUCT)

$(PREFIX_LOCAL)/include/msettings.h:
	cd ../../$(PLATFORM)/libmsettings && make
```

- [ ] **Step 3: Build inside Docker**

```bash
docker run --rm -v "$(pwd)/workspace:/root/workspace" ghcr.io/loveretro/tg5050-toolchain:latest bash -c \
  "cd /root/workspace/all/osdctl && PLATFORM=tg5050 make"
```

Expected: `workspace/all/osdctl/build/tg5050/osdctl.elf` produced

- [ ] **Step 4: Deploy binary and library to skeleton**

```bash
cp workspace/all/osdctl/build/tg5050/osdctl.elf skeleton/SYSTEM/tg5050/bin/osdctl
cp workspace/tg5050/libmsettings/libmsettings.so skeleton/SYSTEM/tg5050/lib/libmsettings.so
```

Note: `LD_LIBRARY_PATH` already includes `$SYSTEM_PATH/lib` (set in `launch.sh`), so `libmsettings.so` will be found at runtime.

- [ ] **Step 5: Commit**

```bash
git add workspace/all/osdctl/osdctl.c workspace/all/osdctl/Makefile skeleton/SYSTEM/tg5050/bin/osdctl skeleton/SYSTEM/tg5050/lib/libmsettings.so
git commit -m "feat: add osdctl CLI bridge for OSD widget ↔ libmsettings integration"
```

---

### Task 2: Rewrite volume slider widget

**Files:**
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/slider_volume_global/set.sh`

- [ ] **Step 1: Rewrite set.sh**

Replace the entire file with:

```sh
#!/bin/sh
OSDCTL="/mnt/SDCARD/.system/tg5050/bin/osdctl"

if [ $# -eq 0 ] ; then
    value=$($OSDCTL get volume)
    mkdir -p /tmp/trimui_osd/slider_volume/
    echo "$value/20" > /tmp/trimui_osd/slider_volume/status
else
    value=$($OSDCTL get volume)
    if [ $1 -eq 0 ] ; then
        value=$((value-1))
        if [ $value -lt 0 ] ; then
            value=0
        fi
    elif [ $1 -eq 1 ] ; then
        value=$((value+1))
        if [ $value -gt 20 ] ; then
            value=20
        fi
    fi
    $OSDCTL set volume $value
    mkdir -p /tmp/trimui_osd/slider_volume/
    echo "$value/20" > /tmp/trimui_osd/slider_volume/status
fi
```

- [ ] **Step 2: Commit**

```bash
git add skeleton/SYSTEM/tg5050/osd/widgets/slider_volume_global/set.sh
git commit -m "feat: rewrite volume OSD widget to use osdctl/libmsettings"
```

---

### Task 3: Rewrite brightness slider widget

**Files:**
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/slider_backlight/set.sh`

- [ ] **Step 1: Rewrite set.sh**

```sh
#!/bin/sh
OSDCTL="/mnt/SDCARD/.system/tg5050/bin/osdctl"

if [ $# -eq 0 ] ; then
    value=$($OSDCTL get brightness)
    mkdir -p /tmp/trimui_osd/slider_backlight/
    echo "$value/10" > /tmp/trimui_osd/slider_backlight/status
else
    value=$($OSDCTL get brightness)
    if [ $1 -eq 0 ] ; then
        value=$((value-1))
        if [ $value -lt 0 ] ; then
            value=0
        fi
    elif [ $1 -eq 1 ] ; then
        value=$((value+1))
        if [ $value -gt 10 ] ; then
            value=10
        fi
    fi
    $OSDCTL set brightness $value
    mkdir -p /tmp/trimui_osd/slider_backlight/
    echo "$value/10" > /tmp/trimui_osd/slider_backlight/status
fi
```

- [ ] **Step 2: Commit**

```bash
git add skeleton/SYSTEM/tg5050/osd/widgets/slider_backlight/set.sh
git commit -m "feat: rewrite brightness OSD widget to use osdctl/libmsettings"
```

---

### Task 4: Rewrite fan level stepper widget

**Files:**
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/stepper_fan_level/set.sh`

- [ ] **Step 1: Rewrite set.sh**

The fan stepper cycles through -1 (auto) to 6 levels. NextUI's `SetFanSpeed()` uses a different scale: -2 (normal/default), -1 (quiet), -3 (performance), 0-100 (manual %).

We need to map the OSD stepper levels to NextUI fan speeds:
- Step -1 (auto) → `SetFanSpeed(-2)` (normal auto curve)
- Step 0 (off) → `SetFanSpeed(0)` (fan off)
- Steps 1-6 → map to manual percentages: 17, 33, 50, 67, 83, 100

```sh
#!/bin/sh
OSDCTL="/mnt/SDCARD/.system/tg5050/bin/osdctl"

FAN_MAX_LEVEL=6
FAN_MIN_LEVEL=-1

# Map stepper level to NextUI fan speed value
# -1=auto(normal), 0=off, 1-6=manual percentages
fan_level_to_speed() {
    case $1 in
        -1) echo "-2" ;;   # auto normal curve
        0)  echo "0" ;;    # fan off
        1)  echo "17" ;;
        2)  echo "33" ;;
        3)  echo "50" ;;
        4)  echo "67" ;;
        5)  echo "83" ;;
        6)  echo "100" ;;
        *)  echo "-2" ;;
    esac
}

# Map NextUI fan speed back to stepper level for display
speed_to_fan_level() {
    case $1 in
        -2|-1) echo "-1" ;;
        0)     echo "0" ;;
        *)
            if [ $1 -le 17 ]; then echo "1"
            elif [ $1 -le 33 ]; then echo "2"
            elif [ $1 -le 50 ]; then echo "3"
            elif [ $1 -le 67 ]; then echo "4"
            elif [ $1 -le 83 ]; then echo "5"
            else echo "6"
            fi
            ;;
    esac
}

if [ $# -eq 0 ] ; then
    speed=$($OSDCTL get fanspeed)
    value=$(speed_to_fan_level $speed)
    mkdir -p /tmp/trimui_osd/stepper_fanlevel/
    echo "$value" > /tmp/trimui_osd/stepper_fanlevel/status
else
    speed=$($OSDCTL get fanspeed)
    value=$(speed_to_fan_level $speed)
    if [ $1 -eq 0 ] ; then
        value=$((value-1))
        if [ $value -lt $FAN_MIN_LEVEL ] ; then
            value=$FAN_MAX_LEVEL
        fi
    elif [ $1 -eq 1 ] ; then
        value=$((value+1))
        if [ $value -gt $FAN_MAX_LEVEL ] ; then
            value=$FAN_MIN_LEVEL
        fi
    fi
    new_speed=$(fan_level_to_speed $value)
    $OSDCTL set fanspeed $new_speed
    mkdir -p /tmp/trimui_osd/stepper_fanlevel/
    echo "$value" > /tmp/trimui_osd/stepper_fanlevel/status
fi
```

- [ ] **Step 2: Commit**

```bash
git add skeleton/SYSTEM/tg5050/osd/widgets/stepper_fan_level/set.sh
git commit -m "feat: rewrite fan level OSD widget to use osdctl/libmsettings"
```

---

### Task 5: Rewrite WiFi toggle widget

**Files:**
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/toggle_wifi/set.sh`
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/toggle_wifi/update.sh`

WiFi doesn't go through libmsettings. NextUI uses `$SYSTEM_PATH/etc/wifi/wifi_init.sh start/stop` and persists state in `minuisettings.txt` via `CFG_setWifi()`. From shell, we call the init script directly, check interface status, and also update `minuisettings.txt` so the setting survives reboot and NextUI's own status indicators stay in sync.

`SYSTEM_PATH` on device is `/mnt/SDCARD/.system/tg5050`.

- [ ] **Step 1: Rewrite set.sh**

```sh
#!/bin/sh
SYSTEM_PATH="/mnt/SDCARD/.system/tg5050"
SETTINGS_FILE="/mnt/SDCARD/.userdata/shared/minuisettings.txt"

wifi_is_on() {
    # Check if wlan0 interface exists and is up
    ip link show wlan0 2>/dev/null | grep -q "UP" && return 0
    return 1
}

update_config() {
    # Update wifi= line in minuisettings.txt for reboot persistence
    if [ -f "$SETTINGS_FILE" ]; then
        sed -i "s/^wifi=.*/wifi=$1/" "$SETTINGS_FILE"
    fi
}

mkdir -p /tmp/trimui_osd/toggle_wifi/

if [ $# -eq 0 ] ; then
    if wifi_is_on; then
        echo 1 > /tmp/trimui_osd/toggle_wifi/status
    else
        echo 0 > /tmp/trimui_osd/toggle_wifi/status
    fi
else
    if wifi_is_on; then
        # Currently on, turn off
        $SYSTEM_PATH/etc/wifi/wifi_init.sh stop > /dev/null 2>&1 &
        update_config 0
        echo 0 > /tmp/trimui_osd/toggle_wifi/status
    else
        # Currently off, turn on
        $SYSTEM_PATH/etc/wifi/wifi_init.sh start > /dev/null 2>&1 &
        update_config 1
        echo 1 > /tmp/trimui_osd/toggle_wifi/status
    fi
fi
```

- [ ] **Step 2: Rewrite update.sh**

```sh
#!/bin/sh
mkdir -p /tmp/trimui_osd/toggle_wifi/
if ip link show wlan0 2>/dev/null | grep -q "UP"; then
    echo 1 > /tmp/trimui_osd/toggle_wifi/status
else
    echo 0 > /tmp/trimui_osd/toggle_wifi/status
fi
```

- [ ] **Step 3: Commit**

```bash
git add skeleton/SYSTEM/tg5050/osd/widgets/toggle_wifi/set.sh skeleton/SYSTEM/tg5050/osd/widgets/toggle_wifi/update.sh
git commit -m "feat: rewrite WiFi OSD widget to use wifi_init.sh directly"
```

---

### Task 6: Rewrite Bluetooth toggle widget

**Files:**
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/toggle_bt/set.sh`
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/toggle_bt/update.sh`

- [ ] **Step 1: Rewrite set.sh**

```sh
#!/bin/sh
SYSTEM_PATH="/mnt/SDCARD/.system/tg5050"
SETTINGS_FILE="/mnt/SDCARD/.userdata/shared/minuisettings.txt"

bt_is_on() {
    hcitool dev 2>/dev/null | grep -q "hci0" && return 0
    return 1
}

update_config() {
    # Update bluetooth= line in minuisettings.txt for reboot persistence
    if [ -f "$SETTINGS_FILE" ]; then
        sed -i "s/^bluetooth=.*/bluetooth=$1/" "$SETTINGS_FILE"
    fi
}

mkdir -p /tmp/trimui_osd/toggle_bt/

if [ $# -eq 0 ] ; then
    if bt_is_on; then
        echo 1 > /tmp/trimui_osd/toggle_bt/status
    else
        echo 0 > /tmp/trimui_osd/toggle_bt/status
    fi
else
    if bt_is_on; then
        # Currently on, turn off
        $SYSTEM_PATH/etc/bluetooth/bt_init.sh stop > /dev/null 2>&1 &
        update_config 0
        echo 0 > /tmp/trimui_osd/toggle_bt/status
    else
        # Currently off, turn on
        $SYSTEM_PATH/etc/bluetooth/bt_init.sh start > /dev/null 2>&1 &
        update_config 1
        echo 1 > /tmp/trimui_osd/toggle_bt/status
    fi
fi
```

- [ ] **Step 2: Rewrite update.sh**

```sh
#!/bin/sh
mkdir -p /tmp/trimui_osd/toggle_bt/
if hcitool dev 2>/dev/null | grep -q "hci0"; then
    echo 1 > /tmp/trimui_osd/toggle_bt/status
else
    echo 0 > /tmp/trimui_osd/toggle_bt/status
fi
```

- [ ] **Step 3: Commit**

```bash
git add skeleton/SYSTEM/tg5050/osd/widgets/toggle_bt/set.sh skeleton/SYSTEM/tg5050/osd/widgets/toggle_bt/update.sh
git commit -m "feat: rewrite Bluetooth OSD widget to use bt_init.sh directly"
```

---

### Task 7: Rewrite LED toggle widget

**Files:**
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/toggle_led/set.sh`
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/toggle_led/update.sh`

LED on TG5050 is controlled via `/sys/class/led_anim/max_scale`. Writing 0 turns LEDs off, writing a non-zero value (e.g. 255) turns them on at that brightness.

- [ ] **Step 1: Rewrite set.sh**

```sh
#!/bin/sh
# TG5050 has 3 LED zones that must all be controlled together
LED_MAIN="/sys/class/led_anim/max_scale"
LED_LR="/sys/class/led_anim/max_scale_lr"
LED_F1F2="/sys/class/led_anim/max_scale_f1f2"
LED_DEFAULT_BRIGHTNESS=255

led_is_on() {
    value=$(cat $LED_MAIN 2>/dev/null)
    [ "$value" != "0" ] && [ -n "$value" ] && return 0
    return 1
}

set_all_leds() {
    echo $1 > $LED_MAIN 2>/dev/null
    echo $1 > $LED_LR 2>/dev/null
    echo $1 > $LED_F1F2 2>/dev/null
}

mkdir -p /tmp/trimui_osd/toggle_led/

if [ $# -eq 0 ] ; then
    if led_is_on; then
        echo 1 > /tmp/trimui_osd/toggle_led/status
    else
        echo 0 > /tmp/trimui_osd/toggle_led/status
    fi
else
    if led_is_on; then
        # Currently on, turn off
        set_all_leds 0
        echo 0 > /tmp/trimui_osd/toggle_led/status
    else
        # Currently off, turn on
        set_all_leds $LED_DEFAULT_BRIGHTNESS
        echo 1 > /tmp/trimui_osd/toggle_led/status
    fi
fi
```

- [ ] **Step 2: Rewrite update.sh**

```sh
#!/bin/sh
LED_MAIN="/sys/class/led_anim/max_scale"
mkdir -p /tmp/trimui_osd/toggle_led/
value=$(cat $LED_MAIN 2>/dev/null)
if [ "$value" != "0" ] && [ -n "$value" ]; then
    echo 1 > /tmp/trimui_osd/toggle_led/status
else
    echo 0 > /tmp/trimui_osd/toggle_led/status
fi
```

- [ ] **Step 3: Commit**

```bash
git add skeleton/SYSTEM/tg5050/osd/widgets/toggle_led/set.sh skeleton/SYSTEM/tg5050/osd/widgets/toggle_led/update.sh
git commit -m "feat: rewrite LED OSD widget to use sysfs directly"
```

---

### Task 8: Rewrite rumble toggle widget

**Files:**
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/toggle_rumble/set.sh`
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/toggle_rumble/update.sh`

Rumble uses GPIO: `/sys/class/gpio/gpio236/value` (1=on, 0=off) and `/sys/class/motor/level` for strength. For a simple toggle, we just pulse it briefly on toggle-on to give feedback, but the real "rumble enabled" state needs a flag file since there's no persistent sysfs state for "haptics enabled".

Looking at the original code, this is a toggle for the haptics setting (enabled/disabled), not a one-shot rumble. We'll use a state file.

- [ ] **Step 1: Rewrite set.sh**

```sh
#!/bin/sh
RUMBLE_STATE="/tmp/trimui_osd/toggle_rumble/enabled"
RUMBLE_GPIO="/sys/class/gpio/gpio236/value"

mkdir -p /tmp/trimui_osd/toggle_rumble/

# Initialize state file from current setting if it doesn't exist
if [ ! -f "$RUMBLE_STATE" ]; then
    echo 1 > "$RUMBLE_STATE"
fi

if [ $# -eq 0 ] ; then
    value=$(cat "$RUMBLE_STATE" 2>/dev/null)
    [ -z "$value" ] && value=1
    echo $value > /tmp/trimui_osd/toggle_rumble/status
else
    value=$(cat "$RUMBLE_STATE" 2>/dev/null)
    [ -z "$value" ] && value=1
    if [ "$value" -eq 1 ] ; then
        # Currently on, turn off
        echo 0 > "$RUMBLE_STATE"
        echo 0 > /tmp/trimui_osd/toggle_rumble/status
    else
        # Currently off, turn on — give brief haptic feedback
        echo 1 > "$RUMBLE_STATE"
        echo 1 > $RUMBLE_GPIO 2>/dev/null
        sleep 0.1
        echo 0 > $RUMBLE_GPIO 2>/dev/null
        echo 1 > /tmp/trimui_osd/toggle_rumble/status
    fi
fi
```

- [ ] **Step 2: Rewrite update.sh**

```sh
#!/bin/sh
RUMBLE_STATE="/tmp/trimui_osd/toggle_rumble/enabled"
mkdir -p /tmp/trimui_osd/toggle_rumble/
if [ ! -f "$RUMBLE_STATE" ]; then
    echo 1 > "$RUMBLE_STATE"
fi
value=$(cat "$RUMBLE_STATE" 2>/dev/null)
[ -z "$value" ] && value=1
echo $value > /tmp/trimui_osd/toggle_rumble/status
```

- [ ] **Step 3: Commit**

```bash
git add skeleton/SYSTEM/tg5050/osd/widgets/toggle_rumble/set.sh skeleton/SYSTEM/tg5050/osd/widgets/toggle_rumble/update.sh
git commit -m "feat: rewrite rumble OSD widget to use GPIO sysfs directly"
```

---

### Task 9: Rewrite mute toggle widget

**Files:**
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/toggle_mute/set.sh`

Currently empty (0 bytes). Implement using `osdctl`.

- [ ] **Step 1: Write set.sh**

```sh
#!/bin/sh
OSDCTL="/mnt/SDCARD/.system/tg5050/bin/osdctl"

mkdir -p /tmp/trimui_osd/toggle_mute/

if [ $# -eq 0 ] ; then
    value=$($OSDCTL get mute)
    echo $value > /tmp/trimui_osd/toggle_mute/status
else
    value=$($OSDCTL get mute)
    if [ "$value" -eq 1 ] ; then
        $OSDCTL set mute 0
        echo 0 > /tmp/trimui_osd/toggle_mute/status
    else
        $OSDCTL set mute 1
        echo 1 > /tmp/trimui_osd/toggle_mute/status
    fi
fi
```

- [ ] **Step 2: Commit**

```bash
git add skeleton/SYSTEM/tg5050/osd/widgets/toggle_mute/set.sh
git commit -m "feat: implement mute OSD widget using osdctl/libmsettings"
```

---

### Task 10: Fix static widget stubs

**Files:**
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/static_cpu_freq/set.sh`
- Modify: `skeleton/SYSTEM/tg5050/osd/widgets/static_temperature/set.sh`

These have broken shebangs. The `refresh.sh` files already work correctly (read from sysfs). The `set.sh` just needs to be a valid no-op since static widgets don't have actions.

- [ ] **Step 1: Fix static_cpu_freq/set.sh**

```sh
#!/bin/sh
# Static display widget — no action on click
```

- [ ] **Step 2: Fix static_temperature/set.sh**

```sh
#!/bin/sh
# Static display widget — no action on click
```

- [ ] **Step 3: Commit**

```bash
git add skeleton/SYSTEM/tg5050/osd/widgets/static_cpu_freq/set.sh skeleton/SYSTEM/tg5050/osd/widgets/static_temperature/set.sh
git commit -m "fix: fix broken shebangs in static OSD widget scripts"
```

---

### Task 11: Test on device

- [ ] **Step 1: Deploy to SD card**

Copy the updated `skeleton/SYSTEM/tg5050/` to the SD card's `.system/tg5050/` directory. Ensure `osdctl` binary is at `.system/tg5050/bin/osdctl` and is executable.

- [ ] **Step 2: Test volume slider**

Open OSD overlay, adjust volume slider up and down. Verify:
- Volume actually changes (audible)
- OSD display updates correctly (X/20)
- Volume persists after closing OSD

- [ ] **Step 3: Test brightness slider**

Adjust brightness slider. Verify screen brightness changes immediately.

- [ ] **Step 4: Test WiFi toggle**

Toggle WiFi on/off. Verify:
- `wlan0` interface appears/disappears
- Can connect to known networks after enabling

- [ ] **Step 5: Test BT toggle**

Toggle Bluetooth on/off. Verify:
- `hcitool dev` shows/hides hci0
- Previously paired devices can reconnect

- [ ] **Step 6: Test LED toggle**

Toggle LED on/off. Verify LEDs physically turn on/off.

- [ ] **Step 7: Test fan level stepper**

Cycle through fan levels. Verify:
- Auto mode works
- Manual levels produce audible fan speed changes
- Fan off actually stops the fan

- [ ] **Step 8: Test mute toggle**

Toggle mute on/off. Verify audio mutes/unmutes.

- [ ] **Step 9: Final commit**

After all tests pass, create a summary commit if any fixes were needed during testing.
