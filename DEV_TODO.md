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

## nextui: renaming a ROM must update collection records that reference it

**Requested:** 2026-07-30, after on-device verification of the folder-game rename
(renaming `Multidisc Test` → `mdisk` left `Collections/qq.txt` pointing at the old
`.m3u` path, so the game silently dropped out of the collection — `getCollection`
skips lines whose path no longer exists).

`doRename`/`renameRomFiles` (`gamelist.c`) already sweep saves, states and art to the
new basename, but never touch `Collections/*.txt`. Fix: after a successful rename, scan
every `Collections/*.txt` for lines referencing the renamed entry and rewrite them
in place.

- [ ] Plain ROM rename: rewrite lines whose SD-relative path matches the old rom path
      (the collection stores the exact path `addRomToCollectionFile` wrote).
- [ ] Folder-game rename: the collection line points at the folder-named cue/m3u
      INSIDE the folder, so two path components change
      (`/Roms/DC/Old/Old.m3u` → `/Roms/DC/New/New.m3u`) — match on the old folder
      prefix + old basename, not string equality alone.
- [ ] Check `Collections/map.txt` (display-alias map, see `content.c`'s map handling)
      for path-keyed entries and rewrite those too if they exist.
- [ ] Same sweep on DELETE is the natural sibling (prune lines referencing the deleted
      rom/folder) — decide whether to include it while in there; today a delete also
      leaves dangling collection lines, they're just invisible because
      `getCollection` filters non-existent paths.

**Recorded:** 2026-07-30, found during review of the folder-game context-menu change.

`gamelist.c`'s BTN_A branch reads `entry->type` after `Entry_open(entry)` (around
`gamelist.c:985`). `Entry_open` → `openDirectory` can free the whole directory stack on
its non-direct-subdirectory path (`launcher.c:512`), leaving `entry` dangling — the same
defect class the folder-game change fixed at its two netplay sites with a
`bool was_dir = entry->type == ENTRY_DIR;` snapshot taken before the call. Apply the same
one-line snapshot here.

- [ ] Snapshot `entry->type` before `Entry_open` in the BTN_A branch; audit the rest of
      `gamelist.c` for any other `entry` deref after `Entry_open`/`openDirectory`.

---

## Netplay wizard: minarch migration

**Recorded:** 2026-07-29, alongside the DC pre-launch wizard build (see
`DEV_CHECKLIST.md`'s "DC netplay pre-launch wizard" section and
`docs/superpowers/specs/2026-07-29-netplay-prelaunch-wizard-design.md`'s
"Minarch follow-up" section). Scoped out of that effort on purpose — DC/flycast
was Phase 1, minarch is the deliberate follow-up.

Minarch's own netplay (fbneo/fceumm/snes9x/supafaust/picodrive/pcsx + GBA/GB
link) currently lives entirely in the in-game pause menu (Host/Join, hotspot/WiFi,
host list) — good UX, but setup happens after the emulator is already running.
The plan is to move minarch-core paks onto the same `netplay.elf` wizard DC.pak
now uses, dropping the in-game setup:

- [ ] Minarch-core paks run the wizard with **no sync args** (`--serve-dir`/
      `--fetch-to`/`--fetch-files` all omitted) — minarch cores don't need the
      wizard's rsync save sync; whatever save-state handling they already do
      stays as-is.
- [ ] Minarch grows a boot-time path: if `/tmp/netplay_session` is present,
      start the engine directly with `role`/`peer_ip`/`mode` from the session
      file and **skip the in-game Host/Join menus** entirely. Link type per core
      is already a static, hardcoded mapping (the existing core-to-link-type
      table), so it's knowable before launch — nothing new needs designing
      there.
- [ ] Drop (or gate off) the in-game Host/Join entry points once the wizard
      path lands, so there's exactly one way to start a netplay session.
- [ ] **Open design question, not yet decided:** whether in-session
      Disconnect/Status survives the menu removal, and if so, where it lives
      (a slimmed-down pause-menu entry? An overlay toast?). The control channel
      and session file are already sufficient to support it; this is a UX
      decision, not a plumbing one.

The wizard's ports (TCP 55440 control channel, UDP 55441 discovery, TCP 18731
rsync) were deliberately chosen to avoid minarch's existing in-game netplay
ports (55435-55438, 56400/56421), so both implementations can run side by side
during this migration — no coordination needed between the two at the network
level, only at the "which one starts the emulator" level above.

---

## Netplay wizard: `wizard_wifi.c` picker loops don't honor `app_quit`

**Flagged:** Task 6 of the 2026-07-29 netplay pre-launch wizard build (see
`.superpowers/sdd/2026-07-29-netplay-prelaunch-wizard/task-6-report.md`, §8.6,
"M7"). Out of scope for that task's fix round since the affected file
(`workspace/all/netplay-wizard/wizard_wifi.c`) belongs to an earlier task.

`wizard_wifi.c`'s WiFi/hotspot picker and scan loops (`WIZ_PICKER_TIMEOUT_MS`,
120000 ms) don't poll the `app_quit` flag the rest of the wizard's state
machine uses to unwind on a caught signal. In practice this means a `SIGTERM`
followed by a `SIGKILL` while a user is sitting in the picker can take up to
120 s to be noticed at all — and it's exactly this window that makes an
orphaned hotspot AP / stray rsyncd a real, reachable failure mode rather than
a theoretical one, which is why `DEV_CHECKLIST.md`'s netplay section has a
dedicated kill-and-heal device check.

- [ ] Make the picker/scan loops in `wizard_wifi.c` poll `app_quit` on the same
      cadence the rest of the wizard does, so a caught signal unwinds promptly
      instead of running out the full picker timeout.

---

## N64: pin the unmatched `mupen64plus` threads

**Found:** 2026-07-27 on Smart Pro S (reproduced twice, incl. a real user session).
Deliberately left out of the flycast merge (`99985dec`) — the evidence said low impact, and the change
belongs with a measured Brick re-verify.

`launch.sh`'s "pin the busiest `mupen64plus`-named thread" heuristic
(`skeleton/SYSTEM/tg5040/paks/Emus/N64.pak/launch.sh` pinning block and the tg5050 twin)
only pins ONE of the (at least) two non-main threads named `mupen64plus`;
the other keeps the unrestricted 0-7 mask.

- [ ] Add an else-branch to the scan loop that pins every unmatched thread to LITTLE by
      default. `DC.pak`'s `pin_threads()` is the reference pattern.

Measured impact is small — the stray thread's load is bursty init/loading work (~3.6%
during boot, ~0% in live gameplay), and since NxRedux only brings cpu0-1/4(/5) online,
"unrestricted" still lands it on the contended cores. Do this together with the pending
Brick pinning re-verification in `DEV_CHECKLIST.md`, so the fix ships measured rather than
blind (which is how the original masks got here).

---

## Trimui Brick Pro: joystick calibration (stock-parity)

**Requested:** 2026-07-29, after on-device probing of the stock firmware (v1.1.1) confirmed
NX's existing calibration cannot work on this model. Stock OS has a "calibrate joystick"
option, so users migrating to redux will expect one.

**Why NX's current path is a no-op here:** `settings_input.c`'s calibration (entered via
`L3+R3` in Settings → Input Tester) reads raw ADC packets from the hall-stick MCU serial
ports `/dev/ttyAS5` / `/dev/ttyAS7` (19200 baud, 19-byte `0xFF…0xFE` packets). The Brick
Pro has NO `/dev/ttyAS*` nodes at all — its sticks are read by `trimui_inputd` over I2C
(`/dev/i2c-3`), which synthesizes the `TRIMUI Player1` uinput gamepad.

**The acquisition protocol is fully reverse-engineered and hardware-verified
(2026-07-29, live device over adb).** Raw ADC does NOT pass through the event device —
that path was tested and disproved: with the flag set, `js0` emitted ZERO events during
8 s of continuous stick movement (baseline without the flag: 818 events, full-scale
−32603…+32727 sweeps). Instead, stock MainUI reads the stick ADCs **directly over I2C**,
the same way `trimui_inputd` does. Protocol, extracted from the UNSTRIPPED
`/usr/trimui/bin/trimui_inputd` (`_i2c_read` + `trimui_poll_thread_joy_i2c`) and
confirmed live with `i2cdetect` (present on the stock firmware at `/usr/sbin/i2cdetect`):

- Bus `/dev/i2c-3`, two ADC chips at 7-bit addresses **0x28 and 0x29** (the binary
  passes 8-bit 0x50/0x52 and shifts right) — one chip per stick. `i2cdetect -y 3`
  shows exactly these two and nothing else.
- Read = `I2C_RDWR` write-then-read pair: write 1 register byte **0xB0**, read
  **4 bytes** = X then Y, little-endian u16 each, 12-bit range (values sanitized to
  0–4095; matches the factory config's ~1120–3050 span). inputd sets
  `I2C_TIMEOUT=5`, `I2C_RETRIES=1`, opens and closes the fd around every poll.
- Flag semantics (verified live): `/tmp/joypad_testmode` present → inputd SKIPS the
  poll entirely (also skipped while `/tmp/system_suspend` exists) — it stops feeding
  uinput AND releases the bus so the calibrator can be sole reader. This is a
  quiesce flag, not a raw-passthrough mode.
- Config consume side (from the same disassembly): calibrated output =
  `(v − zero) * 32760 / (max_or_min − zero)` clamped, a squared-radius circular
  deadzone (`x² + y² < r²` → 0), and ±0x7f change-hysteresis before an event is
  emitted. Keys parsed: `x_min/x_max/y_min/y_max/x_zero/y_zero` plus `z_min/z_max`
  (analog triggers) and `deadzone`.

The downstream contract is shared with the existing serial path: write the same
`/mnt/UDISK/joypad.config` / `joypad_right.config` files NX already writes (factory
unit shipped with `joypad.config` = center ~2086/2038, range ~1120–3050, deadzone
0.10, and NO `joypad_right.config`), then touch `/tmp/trimui_inputd/cal_update` to
make inputd reload.

**Implementation sketch:** in `settings_input.c`, branch on device (or on the absence
of `/dev/ttyAS5`): instead of `cal_open_serial()` + packet parsing, touch
`/tmp/joypad_testmode`, then during the existing range/center capture phases
(`CAL_RANGE_SECS`/`CAL_CENTER_SECS` UX stays as-is) read both chips via `I2C_RDWR`
(addr 0x28/0x29, reg 0xB0, 4 bytes → X,Y u16 LE). Write the same two config files,
`rm` the flag, touch `/tmp/trimui_inputd/cal_update`. Everything but the ~40-line
sampling function reuses the existing code.

- [x] Chip-to-stick mapping — RESOLVED 2026-07-29 empirically (hold-one-stick sessions
      with a cross-compiled I2C reader; static disassembly trace agrees):
      **0x29 = LEFT stick, 0x28 = RIGHT stick.** Direction/wire notes: values are
      BIG-endian u16 on the wire (inputd `rev16`s them); left stick pushed EAST drove
      X DOWN (~2011 → ~1090), right stick pushed UP drove Y UP (~2040 → ~3025 ≈
      factory y_max) — i.e. raw axes are not consistently oriented, but calibration
      only needs min/max/center so orientation is inputd's problem, not ours. Also
      observed: this unit's real deflection (1072) EXCEEDS the factory config span
      (x_min=1120) — live proof per-unit calibration matters. Both sticks' ADCs
      verified working (bring-up datapoint for the DEV_CHECKLIST "Analog sticks"
      item). Reader tool: ~50-line `i2cread.c` (open `/dev/i2c-3`, `I2C_RDWR`
      write-0xB0-read-4 per chip) built with the `ghcr.io/loveretro/tg5040-toolchain`
      docker image — MUST be linked dynamically: `-static` glibc from that toolchain
      aborts with `FATAL: kernel too old` on the 4.9 kernel, the exact taskset trap
      from 2026-07-27. Binary left at `/tmp/i2cread` on the device (tmpfs — gone
      after reboot); source in the session scratchpad, trivially recreatable from
      the protocol above.
- [ ] Implement the brickpro acquisition branch in `settings_input.c` per the sketch.
- [ ] Decide whether to include trigger (`z_min`/`z_max`) calibration or leave triggers
      at inputd defaults — and find where trigger raw values come from (possibly a
      different register on the same chips, or the SoC keyadc; NOT yet traced).
- [ ] Remove the interim "hide/disable or leave inert" ambiguity: the Brick Pro
      DEV_CHECKLIST bring-up item now expects `L3+R3` calibration to WORK on this model
      once this lands.

Evidence artifacts from the probe session (scratchpad, not committed): pulled
`trimui_inputd` binary, `jsA.bin`/`jsB.bin` js0 captures. Redux install still waiting
on an SD card; implementation itself is NOT hardware-blocked any more — only the
stick-to-chip mapping check is.

---

## Trimui Brick Pro: deferred from the port

Scoped out of the Brick Pro port (`c0da09c7`) on purpose. None of these block the port's
hardware bring-up (that checklist lives in `DEV_CHECKLIST.md`).

- [ ] **PortMaster device entry** — its detection keys off `/proc/device-tree/model`, which
      isn't recoverable from the firmware image. Brick Pro currently resolves to
      `trimui-smart-pro`, exactly as the Brick does today (no regression). To fix: read
      `cat /proc/device-tree/model` on the device and add an entry in
      `workspace/all/portmaster/portmaster.c` (`patch_device_info`) alongside the tg5050
      one. **Blocked on hardware.**
- [ ] **Display calibration / white point** — upstream's `displaycal.h` does not exist in
      this fork at all, so upstream's Brick Pro calibration commits (`64160e99`,
      `45406e12`) were out of scope. Porting white-point correction is its own piece of
      work.
- [ ] **Music widget tile is the wrong size on 1024×768** —
      `skeleton/SYSTEM/osd/common/widgets/app_music/skin/block4x2.png` is 540×260 and
      `block4x2_sel.png` is 544×264, both byte-identical to the 1280×720 versions; the
      1024×768 grid tiles are 556×268 and 560×272. So Brick and Brick Pro draw the music
      widget's 4×2 tile 16 px too narrow and 8 px too short. Pre-existing well before the
      OSD dedup refactor — the reorg only made it visible by putting the two variants side
      by side. **Blocked on an asset:** a 1024×768 pair does not exist anywhere in the
      repo. Until one is produced the file correctly stays in `common/`, since there is
      only one variant to ship.

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


## User-visible branding sweep: NextUI → NX Redux

**Decided:** 2026-07-31 (owner). Scope the rebrand to what users actually read,
not the codebase's identifiers.

**Do NOT mass-rename code identifiers or filenames.** `nextui.elf`,
`workspace/all/nextui/`, internal symbols, struct/function names and the like
stay as `nextui`. Upstream NextUI PRs are still ported into this fork, and a
wholesale identifier rename would turn every future port into a merge conflict.
NextUI itself set the precedent by keeping `MinUI.pak` / `MinUI.zip` long after
diverging from MinUI.

**DO sweep user-VISIBLE strings only** — anything rendered on screen or shown to
the user — and rename `NextUI` → `NX Redux` there in a future pass:
- Settings / About screen text.
- Update-splash `TEXT` lines (the `show2` splash strings in the boot / update flow).
- Version strings.
- Any UI-rendered "NextUI" label in the launcher / in-game menus.

Where to look when doing the pass:
- [ ] `workspace/all/settings` — About/Settings screen copy and version display.
- [ ] `show2` splash `TEXT` strings in `boot.sh` / the update flow scripts.
- [ ] `minarch` and `nextui` UI strings (on-screen labels, menu headers, toasts).

Leave doc-link attributions to upstream NextUI docs (e.g.
`nextui.loveretro.games`) intact — those correctly point at NextUI.
