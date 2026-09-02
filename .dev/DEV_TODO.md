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
