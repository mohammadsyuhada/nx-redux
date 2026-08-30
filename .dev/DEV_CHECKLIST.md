# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

Work that is **planned but not yet built** does not belong here — it lives in `DEV_TODO.md`.
Move an entry from there to here once it compiles and needs hardware time.

---

## DC.pak: no coin input for Atomiswave/Naomi arcade games (built 2026-08-30)

GitHub-reported: Atomiswave ROM in the Dreamcast folder plays fine but nothing
inserts a coin. Flycast has no dedicated coin input — arcade titles repurpose
the DC button set, and `DC_BTN_D` IS the Coin key (`maple_cfg.cpp:34` awave,
`maple_jvs.cpp:49` naomi; `DC_DPAD2_UP/DOWN` = Service/Test). Our curated
`SDL_Xbox 360 Controller.cfg` never bound `btn_d`, and SELECT (joystick
button 6) was the one unbound physical button (a real DC pad has no Select).

Fix in both platforms' `skeleton/SYSTEM/<plat>/paks/Emus/DC.pak/`:
`bind11 = 6:btn_d` appended to the mapping's `[digital]` section (flycast's
loader reads bind0..N contiguously and stops at the first gap — appending is
safe, verified in `mapping.cpp:461-488`), and `launch.sh`'s install-if-absent
block grew an elif that upgrades a PRISTINE old install in place: md5-gated on
`0791cba4f099c861a58c9a0473a16361` (the pre-coin shipped file, hash confirmed
against git HEAD), so a user-customized mapping is never overwritten. All
three cases (absent → installed, pristine → upgraded, customized → untouched)
simulated green on host; `sh -n` clean; both platforms' cfgs kept identical.
Service/Test deliberately NOT bound (no free buttons left).

Verified 2026-08-30: the pak exposes NO controller-mapping UI anywhere — the
shared `overlay_settings.json` (pre-launch Emulator Options + in-game overlay)
has only Video/RetroAchievements/Netplay, and flycast's own GUI (the only
Controls page) is patched out (`flycast.patch` reroutes EMU_BTN_MENU →
nx_overlay_request_menu). Flycast writes the mapping file only from that GUI's
setters, so on-device installs are always byte-identical to the shipped file —
the md5 upgrade reaches effectively every install; only a hand-edited (adb/SD)
mapping is spared. Rebinding Service/Test today means hand-editing the cfg.

**FULLY VERIFIED 2026-08-30, including the physical coin press.** Migration:
Brick exercised the md5-UPGRADE branch (seeded pristine old mapping →
replaced in place), TSPS the ABSENT branch (fresh install); zero "Invalid
bind entry" warnings on either. Coin: user supplied Metal Slug 6 (Atomiswave)
+ awbios; on the Brick the title's credit counter went CREDIT(S) 1 → 2 from a
single injected SELECT press (screenshot-proven), and the user confirmed
hands-on. Console-DC regression: SELECT is inert (BTN_D unread by retail DC
games). BIOS gotcha discovered: flycast v2.6 wants the OLD MAME awbios set
(`bios0.ic23`, 128KB); the current MAME re-dump (`bios.ic23_l`, 64KB, crc
e5693ce3) fails "Cannot open bios0.ic23" — repacking it as `bios0.ic23` with
the 64KB image doubled (hardware mirroring) boots fine; both cards now carry
the repacked zip.

- [ ] User commits; reply on the GitHub thread: update, then SELECT = Insert
      Coin (mention the awbios naming: old-set `bios0.ic23` needed, or
      repack a `bios.ic23_l` dump mirrored to 128KB).

---

## Shader flicker with OrigTexture shaders (PT_SkyWalker541) in minarch (built 2026-08-30)

GitHub-reported: PT-SkyWalker541 shaders flicker in mGBA on nx-redux, fine on
NextUI. Root cause: our shader engine predates upstream PR #720 (`e0b0013`,
"shaders_origtexture") which added the `OrigTexture` sampler (texture unit 1)
and `OrigTextureSize` uniform — the contract PT_SkyWalker541 v1.8.0 is written
against. Its vertex stage computes
`orig_coord = TEX0.xy * (TextureSize/InputSize) * (OrigInputSize/OrigTextureSize)`;
with `OrigTextureSize` unset (= vec2(0)) the division goes Inf/NaN, the
fragment stage samples `OrigTexture` at undefined coordinates every frame, and
the white-pixel detection gating its pseudo-transparency flips per frame →
flicker. Core-agnostic (mGBA is just where GBA-focused PT shaders get used);
any post-#720 community shader hits it.

Fixed independently (no upstream code copied — post-#720 upstream is
PolyForm-NC) in `workspace/all/common/generic_video.c`: `Shader` grew
`u_OrigTextureSize`/`u_OrigTexture` (resolved in `PLAT_updateShader`),
`PLAT_GL_Swap` records the pass-0 frame texture + dims
(`orig_texture_gl`/`orig_frame_w/h`), `runShaderPass` uploads OrigTextureSize
and binds the frame texture to unit 1 per draw. Both uploads are gated on
`shader->shader_p`: the chrome passes (final scale/effect/overlay/notif) pass
zero-initialized compound-literal Shader structs, and location 0 in the system
shaders is their `Texture` sampler — ungated, the new sampler upload would
point every chrome pass at unit 1. `OrigInputSize` upload left as-is
(srcw/srch == frame dims for pass 1/srctype=source, which is what PT uses).

minarch.elf compiles clean tg5040+tg5050 (docker toolchains, 2026-08-30).
generic_video.c is #included by both platforms' platform.c, so nextui.elf and
every pak binary also pick it up on their next rebuild — release builds cover
that; only minarch matters for this bug.

**Device-verified 2026-08-30 (both devices):** minarch.elf deployed
Brick+TSPS, PT pack pushed to both cards' `Shaders/` (left installed). Brick:
PT preset in mGBA renders correctly (LCD grid, tint, bezel overlay) and two
composite captures of the static title screen were BYTE-IDENTICAL — the
NaN flicker cannot produce that; title-blink diff localized as expected.
Regression: stock 2-pass `real-gba` (pixellate+lcd3x, pre-#720) renders
perfectly; the Aspect.png overlay chrome pass drew correctly throughout.
TSPS: PT preset verified in-game — the pseudo-transparency machinery itself
(the previously-flickering path) visibly works: light pixels tinted with the
Pocket backing color. Test cfgs cleaned off both cards.

- [ ] User commits; reply on the GitHub issue (fixed, will ship next release;
      shader pack works as-is once updated).

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
