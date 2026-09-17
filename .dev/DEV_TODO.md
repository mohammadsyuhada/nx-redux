# Dev TODO

Work that is **planned but not built** — decided on, scoped, and worth doing, with no code
written yet. Each entry records enough context (why, where, known constraints) that a later
session can start implementing without re-deriving the design.

The two dev to-do files are a pipeline:

| File | Holds | Exit condition |
|---|---|---|
| `DEV_TODO.md` (this file) | designed / requested, nothing written | move the entry to `DEV_CHECKLIST.md` once it builds |
| `DEV_CHECKLIST.md` | built, not yet verified on hardware | delete the section once verified and shipped |

Neither file is a changelog — delete an entry when it lands, don't mark it done and keep it.

---

## Remove the /Emus + /Tools pak cleanup from the updater

**Decided:** 2026-07-31 (owner). The merge itself is built — see
`DEV_CHECKLIST.md`'s "Emus + Tools merged into .system (2026-07-31)" section and
`docs/superpowers/specs/2026-07-31-emus-tools-system-merge-design.md`. This entry
tracks only the deliberate sunset of the transition-period cleanup.

`migrate-paks.sh` (`skeleton/SYSTEM/shared/bin/migrate-paks.sh`, invoked from
`workspace/<plat>/install/update.sh`) deletes tag-matching paks from the `/Emus`
and `/Tools` user layers on **every** update, as a transition measure to clear the
redux-shipped paks that pre-merge cards left shadowing the new
`.system/paks/{Emus,Tools}` copies (§3 of the spec). It is intentionally
destructive to a same-tag copy in the user layer, so it must not run forever.

The teardown has two independently-timed halves — do each only when its timer is
up, and mind the order within each.

**The `/Emus` + `/Tools` pak cleanup** (the always-runs destructive part). After ~2
releases have shipped with it (long enough that essentially every active card has
been cleaned once):

- [ ] Drop the `migrate-paks.sh` invocation from `workspace/<plat>/install/update.sh`
      and delete the script (`skeleton/SYSTEM/shared/bin/migrate-paks.sh`) and its
      test (`scripts/tests/test-migrate-paks.sh`).
- [ ] Update the `/Emus` + `/Tools` README.txt wording: the "same-named paks are
      removed on update" warning becomes wrong once the cleanup is gone — with it
      removed, a same-named pak in `/Emus`/`/Tools` is a usable override via the
      existing SD-wins precedence (`utils.c:439-444`), which is the whole point of
      keeping those folders as override layers.

**The legacy-boot / pre-flatten apparatus** (the shims + the legacy `.system/<plat>`
handling). These are what let an update land straight from v1.4.1's unflattened
layout, so they can only go once updates from v1.4.1 no longer need to work — a
much longer horizon than the pak cleanup, and they must be removed **together**:

- [ ] Delete the "installing legacy-boot compat shims" block in the Makefile
      (`~411-414`, the `install-shim.sh` → `.system/<plat>/bin/install.sh` +
      `minui-launch-shim.sh` → `.system/<plat>/paks/MinUI.pak/launch.sh` copies) and
      the shim sources `workspace/{tg5040,tg5050}/install/{install-shim,minui-launch-shim}.sh`.
      Ordering trap: a pre-flatten card's still-running OLD `install.sh` /
      `MinUI.pak/launch.sh` are what re-exec into the freshly-unzipped tree, so
      dropping the shims while v1.4.1-direct updates still ship makes such an update
      finish **without launching** — it self-heals on the next power-on, but
      migration never ran that boot.
- [ ] Remove migrate-paks.sh's legacy `.system/<plat>` section (step 3, the
      `legacy_sys` prune/remove) and its `/tmp/nx_legacy_boot` (`NX_LEGACY_FLAG`)
      handling in the SAME pass — the flag only exists because a shim set it, so the
      prune-vs-remove branch is meaningless once the shims are gone. (If the pak
      cleanup above already retired all of migrate-paks.sh, this is moot; if not,
      this is the part that outlives it.)

**Not sunset — permanent maintenance.** The device-marker cleanup
`rm -f $SDCARD_PATH/tg5040-brick tg5040-brickpro tg5040-smartpro tg5050-smartpros`
is hardcoded in THREE places — `workspace/tg5040/install/boot.sh`,
`workspace/tg5050/install/boot.sh`, and
`workspace/all/show2/boot-integration-example.sh` — and must be kept in sync with
the Makefile `DEVICES` list every time a device is added. This is ongoing upkeep,
not part of the transition teardown; none of the removals above touch it.

---

## DC pre-launch options: no launch transition into the editor (cosmetic)

**Recorded:** 2026-07-30, noted while moving the pre-launch options gotchas into
`workspace/all/other/flycast/README.md`.

Opening "Emulator Options" from the game-list context menu doesn't play the
usual into-game transition. `gamelist.c`'s case 36 (the pre-launch editor
launch) doesn't set nextui's `startgame` flag, so the screen-blank-out at
`nextui.c:265-268` is skipped and the game list stays visible until the editor
draws over it. Cosmetic only — decide on-device how it actually looks before
choosing whether it's worth setting the flag (which would blank the screen on
the way in, matching a normal launch).

- [ ] On device, watch the transition into the editor from the context menu and
      decide whether to set `startgame` in `gamelist.c` case 36.

---

## Desktop: one shared window for the whole frontend (single-process rewrite)

**Recorded:** 2026-09-02 (user request), after the desktop overlays / frame-pacing /
menu-ghost work landed.

On desktop, launching a game or a tool opens a **new** OS window, and exiting it
closes that window and returns to the nextui window. Cause: the desktop frontend is
a **loop of separate processes**, each creating its own SDL window.
`scripts/desktop/macos-entry.sh` (and the AppImage `AppRun`, via
`scripts/desktop/entry-common.sh`) run `nextui.elf`; opening a ROM/tool makes nextui
write the launch command to `/tmp/next` and **exit** (nextui.c:513-515, "shell
script reads /tmp/next only after nextui.elf exits"); the loop `eval`s that command
(minarch or a tool pak), and when it returns runs `nextui.elf` again. Each iteration
is a fresh process → a fresh `SDL_CreateWindow` (`workspace/all/common/generic_video.c`
~:619, at `SDL_WINDOWPOS_UNDEFINED`). On real hardware there is one framebuffer and
no window manager, so this is invisible — it is purely a desktop artifact of
mirroring the device boot chain.

**Goal:** nextui / minarch / every tool share ONE persistent window for a session.

**Hard constraint (why this is a rewrite, not a tweak):** a window cannot be shared
across processes on macOS — the native handle (`NSWindow*`) is a pointer into one
process's address space and can't be handed to another process.
`SDL_CreateWindowFrom(nativeHandle)` only works cross-process on X11 (window IDs are
X-server resources), so even that trick is Linux-only. The only portable path to a
literal single window is to make the frontend **one process** that loads libretro
cores in-process (dlopen the core + run `retro_run` in the same process/window, the
way standalone RetroArch does) instead of exec'ing a separate `minarch.elf`.

**Shape of the work (large):**
- [ ] A single desktop host process owns the SDL window (created once) and the main
      loop; the nextui menu runs inside it, and "open ROM" loads the core as a
      library and runs the emulation loop in the same window instead of writing
      `/tmp/next` + exiting.
- [ ] Decide minarch's fate on desktop: compile its core-run/UI as a library the
      host calls in-process, or fold its loop into the host. This is the bulk of the
      effort — minarch and nextui are separate binaries with separate main loops,
      config systems (ma_config.c vs nextui), input, and audio setup.
- [ ] Tools (settings/scraper/ratools/extras/... — separate pak binaries): either
      (a) accept that tools still open their own window (partial win; games are the
      common case), or (b) also convert tools to in-process modules (much larger).
      Recommend (a) first.
- [ ] Retire the `/tmp/next` handoff + the `macos-entry.sh` / `AppRun` process loop
      for the in-process paths; keep it only for anything still spawned.
- [ ] Weigh the cost: this forks the desktop frontend structurally from the
      device codebase (device stays multi-process, and must).

**Cheaper alternative (do this if the rewrite isn't pursued):** persist window
position so every process opens its window at the same spot (size is already fixed
1024x768). Today it's created at `SDL_WINDOWPOS_UNDEFINED` (generic_video.c ~:619)
so the OS places/cascades it; reading a saved position and saving on
`SDL_WINDOWEVENT_MOVED` (desktop-gated) makes the window stay put across
transitions. Still a brief close->open flash at each handoff, but no jumping — reads
as one window in place. Low risk, desktop-only, covers nextui/minarch/all tools at
once (all go through generic_video.c). ~90% of the feel for a fraction of the effort.

---

## Convert libretro cheats to DraStic (DS) format for the standalone emulator

**Recorded:** 2026-09-17 (user request), as a follow-up to the Cheat Database feature
(PR #108, branch `worktree-cheat-database`). Spike done this session; feasibility
confirmed; **no code written**. Scope deliberately narrowed to **DS only** — see the
N64/DC note at the bottom for why they're out.

**Why this is needed.** The Cheat Database already downloads libretro's `cheats.zip`
and drops per-system `.cht` files flat into `Cheats/<TAG>/`. For libretro cores,
minarch reads that format directly. But three of the mapped tags (`NDS`, `N64`, `DC`)
are served by **standalone** emulators, not libretro cores, and DraStic (DS) cannot
read libretro's `.cht` wrapper — so the DS files we deliver today are inert. This
entry adds the format conversion so DraStic actually sees them.

**The DS plumbing already exists.** `skeleton/SYSTEM/{tg5040,tg5050}/paks/Emus/NDS.pak/launch.sh`
bind-mounts `$SDCARD_PATH/Cheats/NDS` into DraStic's own `cheats/` dir, and
`.../NDS.pak/devices/*/config/drastic.cfg` ships `enable_cheats = 1`. The cheat DB
already populates `Cheats/NDS` from the libretro folder "Nintendo - Nintendo DS"
(map in `workspace/all/cheatdb/cheatdb_data.c:20`). The **only** gap is file format.

**The conversion is mechanical — same Action Replay codes, different wrapper:**

- libretro side (one INI-ish file per game, many cheats):
  ```
  cheats = N
  cheat0_desc = "Anti-Piracy Bypass Code"
  cheat0_code = "0204E334+E3A00000+0204E338+E12FFF1E"
  cheat0_enable = false
  ```
  Inside `cheatN_code`, individual 8-hex-digit words are joined by `+`
  (verified against a real libretro NDS file — see Sources).
- DraStic side (`<ROM name>.cht`, one file per game):
  ```
  [Anti-Piracy Bypass Code]
  0204E334 E3A00000
  0204E338 E12FFF1E
  ```

- Transform per cheat: strip quotes off `desc`/`code`; split `code` on `+`; flatten
  to a list of 8-hex-digit words (be robust: also split on whitespace, so a token
  that already reads `AAAAAAAA VVVVVVVV` still works); regroup two words per line as
  `WORD1 WORD2`. Emit the header `[desc]`, appending a trailing `+` **only** when
  `cheatN_enable = true` (the `+` = "active by default" in DraStic; the libretro DB
  mostly ships `enable = false`, so cheats land **off** and the user toggles them in
  DraStic's "Configure Cheats" menu). Pass codes through verbatim otherwise.

**The one real caveat — filename keying (matters, discussed with the user).** DraStic
matches a cheat file to a game by **exact filename** minus extension
(`Mario Kart DS (USA).nds` ↔ `Mario Kart DS (USA).cht`), no fuzzy logic of its own,
and the DB ships files under **No-Intro** names. Consequences:
- NX Redux's in-launcher **"Rename Rom" is alias-only** (writes `map.txt`, never
  touches the file on disk; DraStic never reads `map.txt`) — so that kind of rename
  does **not** break matching. This is fine.
- A **physically renamed file** (e.g. renamed over USB to `Mario Kart DS.nds`) will
  **not** find cheats under v1, because the DB only has `Mario Kart DS (USA).cht`.
- Most DS sets are No-Intro-named, so the v1 match rate is high in practice.

### v1 — extract-time, in-place (recommended first)

Simple, host-testable, covers the common (No-Intro-named) case. Breaks only on
physically-renamed files.

- [ ] Add a **pure** converter to `workspace/all/cheatdb/cheatdb_data.c`
      (e.g. `Cheatdb_convertNdsCheat(const char *libretro_text, char *out, size_t cap)`
      or a file-to-file variant), mirroring the existing pure data layer.
- [ ] Host test it in `scripts/tests/test-cheatdb-data.sh` (feed a known libretro
      block, assert exact DraStic output incl. the `+`-header enable mapping and
      word-pairing).
- [ ] In the download/extract loop (`workspace/all/cheatdb/cheatdb.c` `do_download`,
      which calls `Cheatdb_extractFolder` + `Cheatdb_appendManifest` per system),
      **special-case the `NDS` tag**: after extracting, rewrite each `.cht` in
      `Cheats/NDS` from libretro to DraStic format.
- [ ] Make the rewrite **exFAT-safe**: drive the filename list from the **archive
      index** (the same source `Cheatdb_appendManifest` uses — `7zzs l` / `unzip -l`),
      never a live `readdir` of the just-written dir. Opening each known path to
      read+rewrite is fine; *listing* the fresh dir is the hazard (stale entries —
      this is the exact race that broke the original installer, fixed in c286b9af).
- [ ] Leave every other tag byte-for-byte unchanged (minarch's libretro path must not
      change). Nothing but DraStic reads `Cheats/NDS`, so converting in place is safe.
- [ ] Idempotent: "Check for updates" re-extracts and re-converts — converting an
      already-libretro file each time is fine; guard against double-converting an
      already-DraStic file if the loop could ever re-see one.

### v2 — launch-time, fuzzy-matched (only if physical renames matter)

Survives any rename. More moving parts; layer on later if v1's match rate proves
insufficient.

- [ ] Keep the libretro NDS **source** files in a staging dir (NOT `Cheats/NDS`,
      which DraStic reads and can't parse as libretro).
- [ ] At DS launch (`NDS.pak/launch.sh`), for the exact ROM being launched, pick the
      best-matching source via the existing ranker in
      `workspace/all/minarch/ma_cheat_match.c` (strips region tags, ranks
      `Mario Kart DS` → `Mario Kart DS (USA)`), convert it, and write
      `Cheats/NDS/<exact ROM basename>.cht`.

**N64 / DC — explicitly out of scope (verified this session).** Neither pak exposes
cheats at all, so conversion alone wouldn't help — each needs a whole cheat-exposure
feature built first:
- `N64.pak` (mupen64plus): the binary supports `--cheats` + a **CRC-keyed**
  `mupencheat.txt`, but `launch.sh` never passes the flag and there's no per-cheat
  toggle UI. Cheats key on ROM CRC, not filename.
- `DC.pak` (flycast): flycast has a cheat manager, but only inside its own GUI;
  `launch.sh` boots straight into the ROM with no cheat config, so it's unreachable.

**Sources:**
[DraStic cheat readme (Gamestarter)](https://github.com/bite-your-idols/Gamestarter/blob/master/repository.gamestarter/game.drastic/drastic/drastic_readme.txt),
[DraStic cheat guide](https://drastic-ds.com/viewtopic.php?f=7&t=288),
[libretro-database NDS cht](https://github.com/libretro/libretro-database/tree/master/cht/Nintendo%20-%20Nintendo%20DS).

---
