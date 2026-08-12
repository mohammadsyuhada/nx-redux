# emu-options — pre-launch options editor (`options.elf`)

Schema-driven editor for emulator settings, launched before a game runs:
from a game's context menu ("Emulator Options", per-game overrides) or from
Tools → Emulator Settings (console-wide). Adopters: flycast (DC),
mupen64plus (N64) via the `--ini`/`--override` INI backend, and every
minarch-core pak via the `--minarch-*` flat-cfg backend (`opts_minarch.c`).
A pak opts in by shipping an `options.sh` next to its `launch.sh` — that
file's existence is the capability marker nxredux probes; nothing else is
wired per-emulator.

## minarch schema cache

minarch core options are registered by the core at runtime, so their editor
schema is generated, not hand-written: `minarch.elf --dump-options
<core.so> <out.json>` loads the core without a game and serializes its
option definitions (`ma_opts_schema.c`); a launch-time hook refreshes the
same cache (write-if-changed) whenever a core registers options during real
play. `options.sh` regenerates the cache when it is missing or older than
the core `.so`.

## Gotchas (permanent behavior, not bugs)

- **Late-registering cores (FBNeo) have no pre-launch schema until a game
  has been launched once.** FBNeo builds its option list (incl. per-game
  DIP switches) inside `retro_load_game`, which `--dump-options`
  deliberately never calls — the dump exits 1 with nothing captured and the
  editor shows "Settings unavailable". The launch-time cache refresh fills
  the schema on the first real game launch; the editor works from then on
  (with whatever DIP set the last-launched game registered). Every other
  shipped core registers at `retro_set_environment`/`retro_init` and gets a
  schema on the very first editor open, before any launch.
- **Value lists are uncapped** — per-item value/label arrays are
  heap-allocated to actual size (`item_reserve_values`,
  `emu_overlay_cfg.c`), so even gambatte's 325-palette enum is emitted and
  edited in full. Long label-less arithmetic integer lists (vice's
  crop/color sliders) are still emitted as `int` ranges — a slider beats a
  100-entry cycle. A cfg value missing from the schema list (stale cache,
  hand-edited file, newer core) is interned as an extra entry at load
  (`emu_ovl_cfg_enum_intern`), so it displays truthfully and survives a
  full per-game snapshot instead of silently reverting to the default.
- **Locked options** (`-key = value` in any cfg layer) are hidden from the
  editor and their lines pass through writes byte-for-byte. Paks use this
  to pin options away from users (e.g. GBA.pak locks `gpsp_save_method`).
- **Per-game files are full snapshots, shared with minarch's in-game "Save
  for game".** The editor's "Clear All Overrides" reverts only schema
  (core-option) keys to console values and keeps the file whenever anything
  non-schema (frontend options, binds) lives in it — it deletes the file
  only when the result would be fully redundant with the console tier.
