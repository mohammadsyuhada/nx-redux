# System Architecture & Conventions

Cross-cutting mechanisms and conventions that aren't obvious from any single
file. Component-level docs: [BUILD.md](BUILD.md) (build system, code layout),
[OSD.md](OSD.md), [CAPTURE.md](CAPTURE.md), [AUDIO.md](AUDIO.md).

## Process model: the launch loop

`MinUI.pak/launch.sh` (per platform, under `skeleton/SYSTEM/<plat>/paks/`) is
the supervisor. It boots the daemons (keymon, audiomon, `trimui_osdd`, …),
mounts the OSD overlay, then loops: run `nextui.elf`; on exit, if `/tmp/next`
exists, eval it (that's how games and tool paks launch) and restart nextui
when the app exits.

- `/tmp/nextui_exec` existing is what keeps the loop alive; nextui exiting
  with no `/tmp/next` and no exec flag means shutdown.
- `/tmp/poweroff` routes shutdown through `poweroff_next`
  (`workspace/all/poweroff_next`, shared source, AXP2202 PMIC poke gated to
  tg5040): SIGTERM→poll `/proc`→SIGKILL, unmount the SD (realpath'd — see
  [DEVICES.md](DEVICES.md)), then cut power.
- SDL2 turns a SIGTERM into `SDL_QUIT`; `PAD_poll` maps `SDL_QUIT` to
  `PWR_powerOff` — that's the graceful teardown path for nextui and minarch.
  Note the OSD input gate in `PLAT_pollInput` drains events while the OSD
  panel is up and must (and does) still honor `SDL_QUIT` from that drain —
  discarding it once made shutdown hang for the 10 s watchdog.

Games: emu paks run `minarch.elf <core> <rom>`. Resume-slot files
(`.userdata/shared/.minui/<EMU>/<rom>.txt`) are written only by minarch's
save/load-state paths — MENU+SELECT quit-to-switcher saves; in-menu Quit does
not. `/tmp/resume_slot.txt` carries the slot to load into the next launch
(standalone emulator paks consume it via `emu_overlay.c`).

## Rendering model: dirty-flag

nextui and the pak UIs render only when something changed (input, animation,
status flip) — there is no per-frame redraw loop. Consequences show up
everywhere:

- Anything that must appear without input needs to set the dirty flag
  (`UI_statusBarChanged`, indicator flips in `PWR_update`, animations
  registering force-dirty hooks).
- `/dev/fb0` (tg5040) holds a stale frame until the next redraw — affects
  screenshots ([TESTING.md](TESTING.md)) and capture
  ([CAPTURE.md](CAPTURE.md)).
- Any blocking modal loop must keep calling `PWR_update` (auto-sleep, power
  button) and gate its redraws on dirty; `UI_modalLoop`/`UI_confirmModal` in
  `common/ui/ui_confirmdialog.c` are the canonical PWR-aware helpers.
- After a blocking modal returns inside a menu handler, call
  `PAD_poll(); PAD_reset();` — otherwise the caller's own back/press checks
  see the modal's final button event and act on it again.

## Config system

Settings live in `.userdata/shared/minuisettings.txt`, cached fully in memory
per process; `CFG_sync()` rewrites the whole file (content-compared first).
Hard-won semantics:

- External edits are clobbered by any process's later sync — see
  [TESTING.md](TESTING.md) for the adb edit recipe.
- The `wifi=`/`bluetooth=` keys are special: the **on-disk values are
  authoritative** in `CFG_sync` unless the process explicitly toggled them
  (`cfg_wifi_explicit`/`cfg_bt_explicit` in `common/config.c`). Never "fix"
  this back to capturing live radio state — a sync during a transient
  radio-down window (boot, suspend, teardown) then persists `wifi=0` and the
  next boot hard-disables WiFi. The OSD toggles sed their own lines into the
  file, which this contract protects.
- `nextval.elf` links config.c and `CFG_init` ends in `CFG_sync` — it runs at
  boot before the radio is up, which is exactly the poisoning scenario above.
- Radio state reads are live (`PLAT_wifiEnabled` reads sysfs, BT via HCI
  ioctl), never cached config. Passive connectivity checks go through
  `Wifi_isConnected()` (common/wifi.c); interactive gates through
  `Wifi_ensureConnected()`.

## LED control

`LEDS_setProfile` (common/api.c) is the single choke point every app's
relight path goes through (startup, charging, sleep, ambient). The OSD LED
toggle works by touching `/tmp/leds_disabled` (`LEDS_DISABLED_PATH`) —
`LEDS_setProfile` forces the OFF profile while it exists. Writing sysfs
brightness directly is futile: the next app launch re-applies
`ledsettings.txt`.

## UI conventions

- **Hint-bar order**: pairs render left→right and must match physical button
  positions — B (back/exit/cancel) first, then middle buttons Y/X, then A
  last. Non-face buttons (START/MENU leading, SELECT/L-R trailing) keep their
  app-local placement. On empty-state pages the centered empty-state buttons
  carry the hints and the bottom bar is dropped.
- Multi-pair hint bars overflow the 1024-wide Brick — keep bars short; use
  scroll chevrons or empty-state buttons instead of more pairs.
- Text on the THEME_COLOR1 selection pill must use `ALT_BUTTON_TEXT_COLOR`
  (what `GFX_blitButton` uses), never plain white — invisible when a theme
  sets the pill light.
- Button glyphs in hint bars are pre-scaled PNG assets
  (`skeleton/SYSTEM/res/nav_*@{2,3}x.png`, generated by
  `scripts/gen-nav-icons.py` from the 128×128 masters; idempotent, has
  `--check`). They exist because runtime-drawn shapes on the RGB565 screen
  can't anti-alias; see the AA note in [DEVICES.md](DEVICES.md).
- Shared UI widgets live in `common/ui/`, one component per file, registered
  in `ui.mk` only ([BUILD.md](BUILD.md)).

## Feature storage formats

- **Collections**: plain `.txt` under `/mnt/SDCARD/Collections/`, one
  SD-relative path per line (`/Roms/GBA/Game.gba`).
- **Simple mode + PIN**: the flag file
  `.userdata/shared/enable-simple-mode` exists ⇒ simple mode; its *content*
  is the 4-digit Settings PIN (empty/malformed = ungated; delete the file
  from the SD as recovery). Plaintext by design — a convenience gate, not
  security.
- **ROM↔pak mapping**: the `(TAG)` suffix of the ROM's parent folder picks
  `<TAG>.pak` ([PAKS.md](PAKS.md)); `map.txt` in a ROM folder aliases
  filenames to display names (filename-keyed, tab-separated).
- **Rename invariant**: minarch's tag == nextui's `getEmuName(rom_path)`, and
  every derived file (art, saves, states, slot files) shares the ROM's
  `<base>.` prefix — so renaming a ROM is one prefix-boundary sweep across
  the rom dir, `.media`, `Saves/<emu>`, `.minui/<emu>`, and the `<emu>-*`
  state dirs (`renameSweepDir` in nextui).

## RetroAchievements

Everything is **softcore, online and offline** — hardcore was removed
deliberately (unapproved emulators are softcore-locked server-side and
hardcore use risks account flags); don't reintroduce a toggle. Offline play
goes through the server-call interception shim `common/ra_offline.{c,h}`
(host-testable, `common/tests/`), which caches rc_client responses by POST
`r=` param and journals unlocks to `pending/unlocks.jsonl` with a
`confirmed.jsonl` sidecar; sync replays them via real signed rc_api awards.
rcheevos 12.x quirks: the game payload request is `achievementsets` (not
`patch`), startsession is keyed by game id, and award requests are md5-signed
so they must be built via `rc_api_init_award_achievement_request`.
