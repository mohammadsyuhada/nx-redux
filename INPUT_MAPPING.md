# Trimui Brick (TG5040) Input Mapping

## Buttons

| Physical | Linux Event | Hex | SDL Button | Drastic Value |
|----------|-------------|-----|------------|---------------|
| A | BTN_B | 0x131 | 1 | 1025 |
| B | BTN_A | 0x130 | 0 | 1024 |
| X | BTN_Y | 0x134 | 3 | 1027 |
| Y | BTN_X | 0x133 | 2 | 1026 |
| L | BTN_TL | 0x136 | 4 | 1028 |
| R | BTN_TR | 0x137 | 5 | 1029 |
| Select | BTN_SELECT | 0x13a | 6 | 1030 |
| Start | BTN_START | 0x13b | 7 | 1031 |
| Menu | BTN_MODE | 0x13c | 8 | 1032 |
| L3 | BTN_THUMBL | 0x13d | 9 | 1033 |
| R3 | BTN_THUMBR | 0x13e | 10 | 1034 |

## D-pad (HAT mode — default)

| Direction | Linux Event | Value | Drastic Value |
|-----------|-------------|-------|---------------|
| Up | ABS_HAT0Y | -1 | 1089 |
| Down | ABS_HAT0Y | +1 | 1092 |
| Left | ABS_HAT0X | -1 | 1096 |
| Right | ABS_HAT0X | +1 | 1090 |

## D-pad (Joystick mode — FN toggle)

| Direction | Linux Event | Value | SDL Axis |
|-----------|-------------|-------|----------|
| Up | ABS_Y | -32767 | axis 1 negative |
| Down | ABS_Y | +32767 | axis 1 positive |
| Left | ABS_X | -32767 | axis 0 negative |
| Right | ABS_X | +32767 | axis 0 positive |

## Triggers

| Physical | Linux Event | SDL Axis |
|----------|-------------|----------|
| L2 | ABS_Z | axis 2 |
| R2 | ABS_RZ | axis 5 |

## FN Toggle

| Event | Value | Meaning |
|-------|-------|---------|
| SW_TABLET_MODE | 1 | Joystick mode ON |
| SW_TABLET_MODE | 0 | Joystick mode OFF (hat mode) |

## Notes

- Input device: `TRIMUI Player1` at `/dev/input/event3` (VID=045e PID=028e)
- SDL2 enumerates buttons sequentially from the KEY capability bitfield (BTN_A=0x130 onwards, skipping absent codes)
- Drastic button encoding: `1024 + SDL_button_index`
- Drastic hat encoding: `1088 + SDL_HAT_direction` (UP=1, RIGHT=2, DOWN=4, LEFT=8)
- Physical button labels follow Nintendo layout (A=right, B=bottom) but Linux event names follow Xbox layout (BTN_A=bottom, BTN_B=right), hence the swap


# Trimui Smart Pro S (TG5050) and Trimui Smart Pro (TG5040) Input Mapping

## Buttons

| Physical | Linux Event | Hex | SDL Button | Drastic Value |
|----------|-------------|-----|------------|---------------|
| A | BTN_B | 0x131 | 1 | 1025 |
| B | BTN_A | 0x130 | 0 | 1024 |
| X | BTN_Y | 0x134 | 3 | 1027 |
| Y | BTN_X | 0x133 | 2 | 1026 |
| L | BTN_TL | 0x136 | 4 | 1028 |
| R | BTN_TR | 0x137 | 5 | 1029 |
| Select | BTN_SELECT | 0x13a | 6 | 1030 |
| Start | BTN_START | 0x13b | 7 | 1031 |
| Menu | BTN_MODE | 0x13c | 8 | 1032 |
| L3 | BTN_THUMBL | 0x13d | 9 | 1033 |
| R3 | BTN_THUMBR | 0x13e | 10 | 1034 |
| Home | KEY_HOMEPAGE | event0 | N/A | Not on gamepad (sunxi-keyboard) |

## D-pad (HAT)

| Direction | Linux Event | Value | Drastic Value |
|-----------|-------------|-------|---------------|
| Up | ABS_HAT0Y | -1 | 1089 |
| Down | ABS_HAT0Y | +1 | 1092 |
| Left | ABS_HAT0X | -1 | 1096 |
| Right | ABS_HAT0X | +1 | 1090 |

## Left Analog Stick

| Direction | Linux Event | SDL Axis |
|-----------|-------------|----------|
| Up | ABS_Y (negative) | axis 1 negative |
| Down | ABS_Y (positive) | axis 1 positive |
| Left | ABS_X (negative) | axis 0 negative |
| Right | ABS_X (positive) | axis 0 positive |
| Click | BTN_THUMBL | button 9 |

## Right Analog Stick

| Direction | Linux Event | SDL Axis |
|-----------|-------------|----------|
| Up | ABS_RY (negative) | axis 4 negative |
| Down | ABS_RY (positive) | axis 4 positive |
| Left | ABS_RX (negative) | axis 3 negative |
| Right | ABS_RX (positive) | axis 3 positive |
| Click | BTN_THUMBR | button 10 |

## Triggers

| Physical | Linux Event | SDL Axis |
|----------|-------------|----------|
| L2 | ABS_Z | axis 2 |
| R2 | ABS_RZ | axis 5 |

## Notes

- Input device: `TRIMUI Player1` at `/dev/input/event4` (VID=045e PID=028e)
- Home button is on `sunxi-keyboard` (event0), not the gamepad — not accessible via SDL joystick API
- SDL2 enumerates buttons sequentially from the KEY capability bitfield (BTN_A=0x130 onwards, skipping absent codes)
- Drastic button encoding: `1024 + SDL_button_index`
- Drastic hat encoding: `1088 + SDL_HAT_direction` (UP=1, RIGHT=2, DOWN=4, LEFT=8)
- Button mapping is identical to Trimui Brick (TG5040) — same KEY capabilities, same SDL enumeration
- Physical button labels follow Nintendo layout (A=right, B=bottom) but Linux event names follow Xbox layout (BTN_A=bottom, BTN_B=right), hence the swap


# Trimui Brick Pro (TG4040) Input Mapping

Runs on the `tg5040` platform build, but wires more inputs than any other
Trimui: analog sticks *and* the Brick's FN keys *and* a Home button.

## Buttons

| Physical | SDL Button | Notes |
|----------|------------|-------|
| B | 0 | same base layout as Brick / Smart Pro |
| A | 1 | |
| Y | 2 | |
| X | 3 | |
| L1 | 4 | |
| R1 | 5 | |
| Select | 6 | |
| Start | 7 | |
| Menu | 8 | |
| L3 | 9 | **stick click** — on the Brick this index is the FN1 key |
| R3 | 10 | **stick click** — on the Brick this index is the FN2 key |
| L4 | 11 | **FN1 key** — Brick Pro only |
| R4 | 12 | **FN2 key** — Brick Pro only |
| Minus (volume down) | 13 | same as Brick |
| Plus (volume up) | 14 | same as Brick |
| Home | 15 | Brick Pro only; mapped to `BTN_HOME` |

The FN keys moving from 9/10 to 11/12 is the reason `BTN_ID_L4` / `BTN_ID_R4`
exist at all — on the Brick they could ride on L3/R3 because it has no sticks.

Because Home enumerates as a gamepad button (index 15) rather than
`KEY_HOMEPAGE`, keymon's instant-OSD-on-Home path (tg5050 only, evdev code 172)
does not apply here: the Brick Pro opens the OSD by long-pressing `MENU`, like
the Brick and Smart Pro.

## Notes

- **These SDL indices are inherited from upstream NextUI's Brick Pro support
  (PR #766 and its `fix: L4 and R4 button mappings` follow-up) and have not yet
  been confirmed on hardware here.** Everything else in the Brick Pro port was
  verified against the stock `sd_recovery_tg4040_brickpro_ver1.1.1_20260717.img`
  rootfs.
- `/usr/trimui/bin/trimui_inputd` exists on the Brick Pro, so the tg5040 boot
  path (which starts the device's own stock input daemon by absolute path rather
  than vendoring one) works unchanged. Its turbo interface is the same
  `/tmp/trimui_inputd/turbo_*` flag files the other models use.
- The Smart Pro's analog-pad power-up GPIOs (PD14/PD18) are **commented out** in
  the Brick Pro's own `runtrimui.sh`, so its sticks need no GPIO poke.

## LED zones (verified against stock firmware)

Five zones, one more than the Brick:

| Zone | sysfs suffix | Brightness node |
|------|--------------|-----------------|
| FN 1 key | `f1` | `max_scale_f1f2` (shared with F2) |
| FN 2 key | `f2` | — (shares F1's node) |
| Top bar | `m` | `max_scale` |
| Joysticks | `lr` | `max_scale_lr` |
| L/R triggers | `rear` | `max_scale_rear` |

Note `lr` means the *joysticks* here, where on the Brick the same node drives the
triggers. Effect nodes follow the usual pattern
(`effect_<zone>`, `effect_cycles_<zone>`, `effect_duration_<zone>`,
`effect_rgb_hex_<zone>`).
