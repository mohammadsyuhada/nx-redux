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
