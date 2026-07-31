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
- **Value lists cap at 32 entries** (`EMU_OVL_MAX_VALUES`, raised via `-D`
  in this module's Makefile — keep in lockstep with `OPTS_SCHEMA_MAX_*` in
  `ma_opts_schema.h`). Long label-less arithmetic integer lists (vice's
  crop/color sliders) are emitted as `int` ranges instead, which sidesteps
  the cap; labeled long lists truncate, but the serializer always keeps the
  core's default among the kept values so a per-game save can never
  silently change an untouched option.
- **Locked options** (`-key = value` in any cfg layer) are hidden from the
  editor and their lines pass through writes byte-for-byte. Paks use this
  to pin options away from users (e.g. GBA.pak locks `gpsp_save_method`).
- **Per-game files are full snapshots, shared with minarch's in-game "Save
  for game".** The editor's "Clear All Overrides" reverts only schema
  (core-option) keys to console values and keeps the file whenever anything
  non-schema (frontend options, binds) lives in it — it deletes the file
  only when the result would be fully redundant with the console tier.
