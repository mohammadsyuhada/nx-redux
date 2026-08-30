# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

Work that is **planned but not yet built** does not belong here — it lives in `DEV_TODO.md`.
Move an entry from there to here once it compiles and needs hardware time.

---

## Rename standardized to map.txt aliases only (built + deployed 2026-08-30)

Context-menu **Rename Rom** no longer renames files for any core — it always edits the
`map.txt` display alias (home dir + Collections maps, created on first rename), exactly
like the old arcade path. `renameRomFiles` and its sweep helpers are deleted; new
`Recents_updateAlias` re-points the recent.txt alias snapshot so Recently Played / the
game switcher show the new name immediately; names with `/` or a leading `.` are
rejected (the dot would `hide()` the entry). Both platforms built clean, deployed to
Brick + Smart Pro S (md5-verified, rebooted, nextui running). Docs updated in
nx-redux-docs (`guide/context-menu.md`, `guide/main-menu.md`) — uncommitted there too.

- [ ] Rename a regular (e.g. GBA) game: list shows the new name; the rom file, save,
      states and box art on disk keep their OLD filenames; `<console>/map.txt` gains a
      `file<TAB>name` line; save/states still load in-game under the new name.
- [ ] Rename the same game again: the map line is updated (no duplicate lines).
- [ ] Rename an arcade zip: unchanged behavior (alias edit, zip untouched, still boots).
- [ ] Recently Played / game switcher show the new name right after the rename
      (no relaunch needed).
- [ ] Rename a game that's in a collection: the collection list shows the new name
      (Collections/map.txt updated), the game still launches from the collection.
- [ ] Folder game (multi-disc) renamed from the console list: folder entry shows the
      new name; folder/cue/m3u untouched on disk; still launches.
- [ ] Delete a rom that has an alias: map line dropped, collection lines dropped
      (regression check for the `removeCollectionLines` simplification).

Follow-up fix (same day): the standalone-emu overlay menu (DC.pak flycast,
N64.pak mupen64plus) titled itself with the raw filename — launch.sh set
`EMU_OVERLAY_GAME` from `basename "$ROM"` and never consulted map.txt (minarch
already aliases via `getAlias`). All four launch.sh (2 paks × 2 platforms) now
look the alias up from `<rom dir>/map.txt` for the title, while the netplay
`--game` handshake keeps the filename-derived `GAME_ID` (the wizard's HELLO
game gate must be identical across devices — wizard_net.c). Deployed to both
devices; verified live on Brick: flycast env shows
`EMU_OVERLAY_GAME=Metal Slug 6` / `EMU_OVERLAY_ROMFILE=mslug6.zip`.

- [ ] Open the DC overlay menu on an aliased game: title shows the alias
      (e.g. "Metal Slug 6"), save/load state still uses the old slots.
- [ ] Same check for an aliased N64 game.
- [ ] DC netplay between the two devices still pairs (game gate unchanged).

---

## Boot: failed MinUI.zip extraction must not brick the boot loop (built 2026-08-01)

Found live on Smart Pro S (fresh install, 2026-08-01): a truncated MinUI.zip
(card pulled before the 230 MB copy flushed) made `.tmp_update/<plat>.sh`
extract nothing, then `rm -f MinUI.zip` unconditionally — every later boot had
no zip, no `.system`, no splash, and fell through to `poweroff`. Looks like a
dead device. Fixed in both `workspace/{tg5040,tg5050}/install/boot.sh`: the
zip is consumed only when unzip succeeds; on failure a show2 error line is
displayed for 10 s and the zip is kept so the next boot retries. The pakz
loop got the same success-gated consume — a corrupt pakz is renamed
`<name>.failed` (kept for diagnosis, but not re-matched by the `*.pakz` glob,
so no per-boot retry nag) and boot continues normally.

- [ ] Happy path: fresh install extracts and launches normally (both devices).
- [ ] Corrupt-zip path: truncate a MinUI.zip on card (`head -c 10M`), boot →
      "Install failed" splash shows ~10 s, MinUI.zip still on card, device
      powers off; replacing the zip and rebooting installs cleanly.
- [ ] Corrupt-pakz path: truncate a pakz on card, boot → "Package install
      failed" splash ~5 s, file renamed `.failed`, system boots normally and
      the next boot does NOT re-attempt it.

---

## Upstream-port + fix round (built 2026-07-27)

**Status:** ten DEV_TODO items implemented 2026-07-27, committed as `1ccc1030`. All
changed elfs + the rebuilt GLideN64 `.so` + N64 launch.sh are pushed to both cards
(Brick and Smart Pro S, md5-verified 2026-07-27).

Full deploy: `make all`, flash zip. Quick iterate: push the single rebuilt `.elf` (reboot required
for nextui/minarch pushes — see Gotchas at the bottom of this file).

### On-device verification

- [ ] **SRAM read unification** (`ma_saves.c`, upstream #667) — save in-game with
      Save Format = SRM (compressed), switch back to the default (uncompressed), relaunch:
      the in-game save must be intact, and after the next in-game save the `.srm` should be
      raw (`head -c8` no longer `#RZIPv1#`). Regression: raw `.srm` still loads, and a
      RetroArch-imported compressed `.srm` loads under the default setting.
- [ ] **Resampler leak fix** (`api.c`, upstream #697) — play any PAL game (or set
      Core Sync = Native) for ~10 min; `VmRSS` in `/proc/<minarch pid>/status` must stay
      flat (before the fix it grew ~11 MB/min).
- [ ] **RETRO_ENVIRONMENT_SHUTDOWN** (`ma_environment.c`, upstream #699) — Doom
      (PRBOOM.pak): in-game menu → Quit must exit cleanly back to nextui. FBNeo: launch a
      known-bad ROM, any button on the error screen must exit. Check the switcher isn't
      left pointing at garbage for the PRBOOM quit (core dies before the menu's autosave —
      quit here goes through the env callback, not ITEM_QUIT, so no slot-9 autosave fires;
      confirm RESUME behaves sanely, i.e. falls back to previous state or START).
- [ ] **Rewind re-init fix** (upstream #728 + early-out) — enable rewind, play: rewind
      works; changing a rewind option mid-game still takes effect (buffer size change →
      re-init happens); in-game "Restore Defaults" no longer hitches for seconds with a
      big rewind buffer; game launch with rewind enabled allocates once (single
      "Rewind:" init in the log, if logging shows it).

### Follow-ups discovered while implementing

- The `keepAwakeUSB` config key is camelCase, matching its immediate neighbours
  (`disableSleep`, `sshOnBoot`) rather than the older lowercase style the DEV_TODO entry
  suggested — deliberate.
- CFG setters were NOT given per-setter early-returns: `CFG_sync()` now compares content
  before writing, which subsumes the I/O benefit (a redundant set costs a read+compare,
  never a write).
- Core-requested SHUTDOWN (env cmd 7) deliberately does NOT trigger the slot-9 autosave —
  it fires mid-`retro_run` where a state save is unsafe, and the quitting core (Doom quit
  menu / failed init) rarely has a moment worth resuming. Revisit only if PRBOOM quit
  verification above shows a bad switcher experience.
