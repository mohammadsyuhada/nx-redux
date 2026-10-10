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

## DC: per-game VMUs, and whether to move flycast to the libretro core

**Requested:** 2026-09-28 (owner), while NX Redux Mobile added Dreamcast on the flycast **libretro** core with one
VMU per game (`Saves/DC/<game>.A1.bin`, mirrored to the user's tree like any battery save).

**Today on the handheld:** `DC.pak` runs **standalone** flycast. `launch.sh` sets `XDG_DATA_HOME="$USERDATA_DIR/data"`,
so flycast keeps its memory cards (`vmu_save_A1.bin` …, one per port/slot, **shared by every game**), `dc_nvmem.bin`
and save states in `.userdata/…/data/flycast/`, not in `Saves/DC/` (which `launch.sh` creates but flycast never
uses). A shared card fills up with many games, and the user can't see or back it up with the other saves.

**Per-game VMUs without changing emulator (recommended first):**
- [ ] Check standalone flycast's per-game VMU option (believed `PerGameVmu` in `core/cfg/option.h`; confirm the key,
      its section in `emu.cfg`, and the per-game file name it writes) and set it in `DC.pak`'s `default.cfg`
      (tg5040 `default-smartpro.cfg` too).
- [ ] Decide where per-game cards live: point them at `$SAVES_PATH/DC/` if flycast allows it, so they sit with the
      other saves; otherwise leave them in the data dir.
- [ ] Migration: existing shared `vmu_save_A1.bin` holds every game's saves. Keep it (don't delete) and document that
      old saves stay on the shared card; optionally leave per-game off for users who already have one.
- [ ] Netplay: the wizard's `--fetch-files "vmu_save_*.bin,dc_nvmem.bin"` and the host backup copy assume shared
      cards; update the file globs for per-game cards.

**Moving the handheld to the flycast libretro core (only if there is a reason beyond VMUs):** not needed for
per-game VMUs. Standalone flycast was chosen for things the launcher builds on — the GGPO netplay wizard,
RetroAchievements through flycast's own libcurl (with our CA-bundle patch), the NX overlay integration and the
positional controller mapping files — which a libretro/minarch move would have to redo or drop, plus a
performance check on the handheld GPU. Revisit only if the standalone build becomes a maintenance burden.

**Performance check done — phase 0 spike, 2026-09-29 (branch `minarch-gpu-spike`): GO.** A minimal GPU
(hardware-render) path in minarch runs flycast libretro v2.6 on the Brick at or above standalone speed with matched
settings (Crazy Taxi 2 82–85 % vs 78–80 %, Soulcalibur fight 73–81 % vs 70–76 %, Metal Slug 6 100 %), and shaders,
menu, save states and screenshots work. Open items (VMU, CPU-speed option, BIOS dir, GGPO loss): `.dev/spikes/minarch-gpu/RESULTS.md`.

**Cross-device saves (later, optional):** even with per-game cards on both, standalone and libretro name the files
differently (libretro per-content: `<content>.A1.bin`); moving a save between phone and handheld needs an agreed
name or a small rename step.

---

## Credit Kenney for the button-hint glyphs

**Requested:** 2026-09-29 (owner). The button-hint glyphs come from Kenney's **Input Prompts** pack (1.5A, CC0: credit
is not required, but the owner wants to give it). Source pack: `~/Downloads/kenney_input-prompts_1.5/` on the owner's Mac.

- [ ] Add a line to the `## Credits` section of `README.md`, e.g.
      `- [Kenney](https://kenney.nl/assets/input-prompts) for the Input Prompts glyphs used in the button hints (CC0)`.
- [ ] Add the same credit to the docs site page `nx-redux-docs/docs/reference/credits.md`.

---

## DC libretro vs standalone: performance figure for the PR and release notes

**Requested:** 2026-09-29 (owner). The PR that moves `DC.pak` to minarch must state how much faster or slower the flycast
libretro core runs than standalone flycast, **with Soulcalibur as the reference**. The figure goes into the release notes.

Method, so the number holds up (the spike figures in `.dev/spikes/minarch-gpu/RESULTS.md` are not a like-for-like comparison):
- [ ] Same build and settings as shipped: the libretro core with the pak's final `default.cfg` (Emulated sync,
      auto-skip `some`, per-game VMU, …); standalone with its shipped `default-brick.cfg` / `default.cfg`. Same
      BIOS mode on both. Smart Pro S fan on Auto.
- [ ] Metric: emulation speed = core audio frames/s ÷ 44 100. Libretro via minarch's `[HWR]` line; standalone via the audio
      probe in a *copy* of `DC.pak` (never replace the installed binary).
- [ ] Scene: Soulcalibur's attract loop from the title screen onward. Average over ≥ 5 min per run, 3 runs per emulator
      per device (Brick + Smart Pro S). Report the mean and range, then libretro ÷ standalone − 1 as the percentage.
- [ ] Put the table plus one summary line per device in the PR description, e.g. "Soulcalibur runs N % faster on the
      Brick (X % vs Y % of full speed)". Keep the raw windows in `RESULTS.md`.

---

## Music player: replace fdk-aac with FFmpeg's native AAC decoder (7.1+)

**Decided:** 2026-10-07 (owner). `libfdk-aac.so` (fdk-aac v0.1.6, a TrimUI-SDK prebuilt in
`workspace/all/musicplayer/include/fdk_aac/lib/`) is under the FDK-AAC license, which the FSF
lists as GPL-incompatible, and `musicplayer.elf` + `musicplayerd.elf` (GPLv3) link it
(`-lfdk-aac`, `workspace/all/musicplayer/Makefile`). A GPLv3 linking exception isn't an option:
the binaries also link `common/` code from many other authors (MinUI, NextUI contributors).

Replace it with a static, minimal libavcodec from **FFmpeg ≥ 7.1** (LGPL-2.1+, no
`--enable-gpl`), float `aac` decoder (7.1 added xHE-AAC/USAC, float decoder only; the float
decoder also has AArch64 NEON, `aac_fixed` doesn't).

- [ ] Build script (toolchain image) for a minimal static FFmpeg 7.1+:
      `--disable-everything --enable-decoder=aac --enable-decoder=aac_latm --enable-parser=aac
      --enable-parser=aac_latm --disable-programs --enable-static --disable-shared`, sha256-pinned
      tarball; build it in CI next to ffplay (could share the FFmpeg version with ffplay's build).
- [ ] Port the four fdk call paths (~30 `aacDecoder_*` calls, `player.c` + `radio.c`):
      `.m4a` via minimp4 (DSI → `extradata`, one `avcodec_send_packet` per MP4 sample), `.aac`
      ADTS files, Icecast/ICY AAC/AAC+ streams, and HLS radio (our TS demux → ADTS). ADTS paths
      need `av_parser_parse2` to split frames (fdk took arbitrary byte chunks via `Fill`).
- [ ] Output is planar float (FLTP): add interleave + clamp to our s16 path. Seek =
      `avcodec_flush_buffers`; take rate/channels from `frame->sample_rate`/`ch_layout`.
- [ ] Check HE-AAC (implicit SBR) `.m4a`: today the rate comes from the stsd box, not the
      decoder, which can be half the real output rate.
- [ ] Device-test LC, HE-AAC v1/v2 radio ("aacp"), m4a seek, HLS rate changes; measure the
      size of the two ELFs (estimate +0.5–0.9 MB each).
- [ ] Remove `include/fdk_aac/` and the `libfdk-aac.so*` copy in the root `Makefile`, and
      `licenses/fdk-aac.txt`.

Fallback if this stalls: faad2 (GPL-2.0-or-later, API close to fdk's, no USAC).

---

## Files tool: replace NextCommander with our own copy of od-contrib/commander (MIT)

**Decided:** 2026-10-07 (owner asked; recommendation recorded). The Files tool is
`LoveRetro/NextCommander` (cloned `--depth 1`, unpinned, in `workspace/<plat>/Makefile`, plus
`workspace/all/other/NextCommander.patch`). Nothing in its lineage has a license — LoveRetro ←
OnionUI ← gcwnow ← the original DinguxCommander — so strictly it is all-rights-reserved and we
can't redistribute it. Forking the *original* DinguxCommander doesn't help: it was published
without a license too.

`od-contrib/commander` (OD Commander, Gleb Mazovetskiy) forked DinguxCommander, rewrote most of
the code, replaced all icons, and added an **MIT** `LICENSE.txt` (commit 6023665431: "The
original code and icons were published without a license. Since then, most of the code has
been rewritten and all of the icons have been replaced"). It supports SDL2 and has
controller-button handling.

**Copy, don't GitHub-fork (decided 2026-10-07):** make our own standalone repo (e.g.
`nx-commander` under the owner's account) — `git clone` od-contrib/commander and push it to a new
empty repo, so the history comes along but the repo is **outside their fork network**. A
GitHub "Fork" survives the owner deleting or privatising their repo, but a DMCA notice that
claims the whole fork network can take every fork down at once; a standalone copy is only hit if
it is named itself. The MIT grant we received can't be revoked either way. The remaining risk is
a claim by the original DinguxCommander author over leftover unlicensed code — that hits any copy
that is named, so the more of it we rewrite, the smaller it gets.

- [ ] Create the standalone repo from od-contrib/commander; keep its MIT `LICENSE.txt` and
      copyright line, and say in its README it is based on od-contrib/commander (Gleb Mazovetskiy).
- [ ] Port what NextCommander adds for us (diff NextCommander against its DinguxCommander base +
      our `NextCommander.patch`: SDL2/TrimUI input, NextUI theming/fonts, screen size) as commits
      in our repo.
- [ ] Build from **our** repo at a pinned full commit hash, never from upstream (swap the
      unpinned `--depth 1` clone in `workspace/<plat>/Makefile`, and the copy in
      `workspace/<plat>/platform/Makefile.copy`); keep `Files.pak` paths/behaviour unchanged.
      Optionally attach its source tarball to each GitHub release so shipped code always has its
      source beside it.
- [ ] Replace `licenses/nextcommander.txt` with the OD Commander MIT text; update the README
      credit line (`skeleton/BASE/README.txt`, "Licenses and credits").
- [ ] Device-test on Brick + Smart Pro S (browse, copy/move, text/image viewer, keyboard).
