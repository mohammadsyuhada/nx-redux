# Dev Checklist

Running checklists for work that is **built but not yet verified on hardware**, so a later
session (or another person) can pick up the bring-up without re-deriving what is already known.

One section per in-flight effort. When a section is fully checked off and shipped, delete it —
this file is a to-do list, not a changelog.

Work that is **planned but not yet built** does not belong here — it lives in `DEV_TODO.md`.
Move an entry from there to here once it compiles and needs hardware time.

---

## DC netplay (GGPO + DCNet) overlay wiring (built 2026-07-28)

**Status:** the first live pair test (2026-07-28, Brick ↔ Smart Pro S, MvC2) **SUCCEEDED** —
both devices connected over GGPO and mirrored in lockstep, with no manual file copy and no
IP ever typed. It exposed two defects, and **v2 fixes both**
(`docs/superpowers/plans/2026-07-28-netplay-symmetric-discovery.md`): (1) MvC2's VS mode was
unusable because Dreamcast port B was empty (`device2 = MDT_None`) — GGPO always drives port B
as player 2, the remote player on the host but the LOCAL player on the client, which is exactly
why the second device's Start did nothing, so BOTH machines need a pad there; launch.sh now sets
`[input] device2 = 0` while GGPO is on and restores `10` when off; (2) discovery was asymmetric
and timing-fragile — the host learned the
client's IP passively from its own server access log inside a pre-launch window, so a client
that was late, absent, or launched first left the host hung forever on "Starting Network"
(no timeout, Cancel only) — both devices now serve a role-tagged `netplay-info` on TCP 19714
and both scan the LAN for the opposite role, so launch order and timing are irrelevant.
Underneath, unchanged from v1: "host brings the memory card" (TCP 19714 HTTP rendezvous, busybox
httpd on tg5040 / python3 http.server on tg5050), isolated client `netplay-data/`, host
`netplay-backup/`; design in `docs/superpowers/specs/2026-07-28-netplay-state-sync-design.md`.
The v2 block is byte-identical in both DC.pak `launch.sh` files and bench-tested on the Brick
via adb (cfg/probe/wget/tar mechanics all PASS, tar guard hardware-verified). v2's two-device
symmetric discovery got its first real pair test on 2026-07-29 and **failed**: the client found
the host, the host never found the client, and both then stalled at flycast's Starting Network
modal. Two independent bugs were root-caused and fixed the same day — an ARP-sweep probe storm
and a shared temp file in `nx_fetch` — see the Busy-LAN discovery entry below for the measured
evidence. Post-fix, `nx_find_peer` finds a peer in 8 s on 3/3 runs against a fake peer on the
same busy `/24`, but **the two-device pair test has not been re-run**; treat every v2 discovery
path below as single-device evidence until it has.
Discovery feedback landed on top of v2 (2026-07-28): a `show2.elf` daemon status screen fed
`TEXT:`/`PROGRESS:` over `/tmp/show2.fifo`, plus two speed fixes (the stored `server =` is only
probed when it is in the current /24, and neighbour probes now run concurrently).
Bench-measured on the Brick (tg5040) 2026-07-29 — **function level only**: `nx_ui_start`
returns rc=0 with a live PID, and the daemon is FIFO-ready and painting in **<=1 s** (three
cold runs; 1 s poll granularity, so the true figure is at or under 1 s — this replaces the
earlier ~3-4 s ESTIMATE, which was wrong by 3-4x); `--image=/dev/null` is accepted at runtime
(`IMG_Load` logs "not a regular file or pipe" and rendering continues); `nx_ui` rc=0 with and
without a progress argument; `nx_ui_stop` removes the FIFO and the daemon is gone in 1 s; with
`show2.elf` off PATH all three are rc=0 no-ops in 0 s. That bench also caught a REAL SHIPPED
BUG: plain `killall` (SIGTERM) does NOT kill show2 — measured alive 6 s later — so the
code now uses `killall -9` (see Gotchas). Confirmed VISUALLY since (user, Brick 2026-07-29, with
the rebuilt show2 driven by hand over adb — not from launch.sh): text renders, the second
`--hint` line renders and persists across `TEXT:` updates, and the marquee animates, freezes on
a numeric `PROGRESS:` and RE-ARMS on a later `PROGRESS:-1` (the re-arm two fix rounds were built
on is now observed, not inferred). The deployed tg5050 binary was then exercised too (Smart Pro
S, 2026-07-29 — that platform had never run show2 at all before): it starts with `--hint`,
creates its FIFO in ~1 s, accepts `TEXT:` and `PROGRESS:` updates and stays alive. **What is
still unverified: the feature has never run in a real game launch on either device.**
"Hardware-verified" here means the shell functions behave and show2 draws what it is told; it
does NOT mean the status screen has been seen driven by a real netplay launch.
**B skips netplay** (built 2026-07-29): during discovery, pressing B stops the search and
launches the game single-player for THAT RUN ONLY — launch.sh starts flycast with `-config
network:GGPO=no`, which flycast keeps as a *virtual* value and never writes back, so `emu.cfg`
still holds `GGPO = yes` afterwards and the next launch attempts netplay again. B is live from
the moment the status screen starts until flycast is started, and is honoured at phase
boundaries, in the discovery loops and inside `nx_fetch`'s watchdog. Bench-verified on the Brick
2026-07-29, 12/12 checks against the functions extracted verbatim from launch.sh: arm/disarm
lifecycle, stale-flag clearing, the flag honoured, and `nx_fetch` aborting 2 s into a 30 s
timeout. The button number is settled on BOTH units: `00`, five consecutive presses each,
raw `.. .. .. .. 01 00 01 00` on the Brick and identical on the Smart Pro S (2026-07-29), so the
shared `NX_CANCEL_BTN=00` is right for both and the byte-identical block needs no per-device
handling. **Still NOT verified, all three needing a real session: (1) an actual B press during a
real game launch — that it cancels, lands in single-player and leaves `[network] GGPO = yes`
afterwards; input cannot be injected over adb, so this has never run end to end; (2) any
two-device pair test with the new block on both units; (3) the cancelled-client-plays-on-its-own
-saves path, which is code-verified and reasoned but never exercised in a real session.**
Flycast itself was not rebuilt — v2.6 already ships GGPO and DCNet compiled in — but `show2.elf`
WAS rebuilt on both platforms for the hint line (new optional `--hint`) and is now **deployed to
both devices** (2026-07-29): Brick md5 2678718c -> 4be3ac43, Smart Pro S fe7d510b -> c943d4b7,
each md5-verified against the local build, syntax-checked with the device's own shell, and
`show2.elf` resolves bare via PATH on both. Rollback needs no computer: the pre-change binary is
kept on-device as `show2.elf.orig` beside it (a second copy lives in the SDD snapshots) — worth
keeping, since show2.elf is ALSO the first-boot install splash. Remaining deploy for this
feature = push `overlay_settings.json` to `$SDCARD/Emus/shared/flycast/` + both DC.pak
`launch.sh` files to their pak dirs on both cards.

### On-device verification

- [ ] **Overlay round-trip** — "Netplay" section renders with all 4 items; after toggling
      and quitting, `emu.cfg` gains the `[network]` keys (`GGPO`/`ActAsServer`/`DCNet` as
      yes/no, `GGPODelay` as int) and existing `[config]`/`[achievements]` values are not
      clobbered (flycast rewrites the whole cfgdb on quit).
- [ ] **GGPO zero-config LAN match (v2 re-run)** — Brick ↔ Smart Pro S, Marvel vs.
      Capcom 2 (byte-identical chd + `dc_boot.bin` on both cards; wifi on). Overlay:
      GGPO on on both, Host on exactly one. Launch order is free (see below); a wait
      of up to 90 s per side is expected while discovery runs, covered by the status
      screen (measured FIFO-ready and painting in <=1 s on tg5040). A pairing fast
      enough to beat even that shows the screen only briefly or not at all, which is
      equally correct. Match must
      connect with NO manual file copy and NO IP ever typed. Verify `[network]
      server =` got auto-written on BOTH devices' emu.cfg. (v1 already passed this
      with host-first ordering — the point of the re-run is that v2 still does.)
- [ ] **P2 controller** — with netplay on, MvC2's VS mode is selectable and the second
      device's Start/inputs register as Player 2; confirm `[input] device2 = 0` appears
      in BOTH devices' emu.cfg. Then toggle netplay off, relaunch, and confirm it went
      back to `10` (single-player sees a one-pad Dreamcast).
- [ ] **Launch order is free** — client-role device launched FIRST, host second: they
      must still pair (this is what v1 could not do).
- [ ] **Repeat-session fast path** — second run with an unchanged peer IP should pair
      almost immediately (stored `server =` probe hits before any ping sweep). Only
      applies on a /24 lease with the stored address inside it; off-subnet or on any
      non-/24 lease the fast path is skipped by design and the sweep runs.
- [ ] **Stale peer IP** — change one device's DHCP lease (or edit `server =` to a dead
      address), relaunch: the sweep must still find the peer and rewrite `server =`.
- [ ] **Two hosts / two clients** — set both devices to the same role: they must NOT
      pair (role line mismatch), and both must fail open rather than hang in launch.sh.
- [ ] **tg5050 serve branch** — RESOLVED 2026-07-28 on-device: Smart Pro S runs
      busybox 1.35.0 with NO `httpd` and NO `nc` applet; `wget`/`tar`/`ping -W`/
      `ip`/`wlan0` all verified present, and the fallback serve branch
      (`python3 -u -m http.server`, python3.10 on firmware) was smoke-tested there
      (serves, wget round-trip). Remaining on-device item: one session ON the Smart
      Pro S in each role to exercise the python branch end-to-end in a real launch
      (both roles now run a server, so the branch is on the client path too).
- [ ] **Role swap** — after a session, swap Host/join roles and reconnect: the new
      host's own solo-unlocked content must be what both players see (its REAL card
      is now the source), not the previous session's borrowed copy.
- [ ] **Client real-save integrity** — md5 the client's `data/flycast/vmu_save_*`
      + `dc_nvmem.bin` before and after a full netplay session: byte-identical
      (client plays entirely in `netplay-data/`).
- [ ] **Tar guard** — the client extracts the synced tar only when all FIVE conditions
      hold: non-empty; `tar tf` lists at least one entry (a non-tar payload lists
      nothing and would pass the negated tests vacuously); every entry is a bare
      expected NAME (`vmu_save_[A-D][12].bin` / `dc_nvmem.bin`, no slashes, no `..`,
      no absolute paths); every `tar tvf` mode column is `-`; and no entry is a link
      (` -> ` — NOT redundant with the mode column, since busybox prints a HARDLINK
      with a leading `-`; flycast would otherwise write VMU data through a link as
      root). Bench-verified on the Brick 2026-07-28 (ACCEPT good tar; REJECT
      path-traversal, symlink, hardlink, non-tar junk, empty) — the on-device item is
      confirming a hand-crafted bad tar is refused inside a REAL launch and it still
      falls open.
- [ ] **Host backup** — after any hosted session: `netplay-backup/` contains
      pre-session copies of `vmu_save_*.bin` + `dc_nvmem.bin`.
- [ ] **Wrong-game refusal** — device A on game A, device B on game B: they must NOT
      pair (netplay-info ROM line mismatch), and each falls open into a normal
      (failing) GGPO launch after its 90 s scan.
- [ ] **Fail-open sanity** — one device launched with GGPO on and no peer anywhere:
      after its 90 s scan it must still reach flycast, never a stall in launch.sh
      (the status screen should be expected to vanish on whichever message survived —
      on the client that is "looking for the host..."; on the host it is EITHER
      "preparing saves..." OR "looking for player 2 on ..." — the host never emits the
      client's string; the "no ... found - starting anyway" update is queued and then
      killed with the daemon, so do NOT judge on seeing it). Expect flycast to sit on
      "Starting Network" (it has no
      timeout — Cancel is the only exit); that is GGPO's behavior, not a launch.sh
      hang, and it is what the user sees whenever `server =` is empty or wrong.
- [ ] **Rollback headroom on Brick** — watch for re-simulation hitches mid-match (GGPO
      forces SH4 to stock 200 MHz; A53s have limited headroom). If hitchy, Input Delay 2–3.
- [ ] **Disconnect behavior** — opening the overlay or save/load state mid-match pauses
      emulation → peer drops after 3 s of silence. Confirm this fails as a clean error/
      return to gameplay, not a hang.
- [ ] **DCNet smoke test** — `DCNet = yes` with an online-capable game if one is on hand
      (ChuChu Rocket, Quake III, PSO); expect the "Connected to DCNet with modem" toast.
      MvC2 is expected to show nothing (no native online mode). Needs internet, not LAN.
- [ ] **UPnP off** — launch.sh now writes `[network] EnableUPnP = no` on every GGPO
      launch, so no hand edit is needed on `.initialized` devices any more. Verify the
      key is present in BOTH devices' live emu.cfg after a netplay launch, and that no
      router port-mapping for UDP 19713 appears (flycast defaults UPnP on and leases the
      mapping for 24 h).
- [ ] **UPnP on an upgraded install** — start from the real case for every existing
      device: `EnableUPnP` absent from emu.cfg entirely. Launch with GGPO on and confirm
      no UDP 19713 mapping appears on the router, in BOTH roles (`ggpo.cpp:801-804`
      punches the mapping before the `ActAsServer` branch, so host and client both would).
- [x] **Busy-LAN discovery** — this is where the first real pair test failed, and it took
      two independent bugs to explain it. Both are fixed and re-verified on the Brick
      (2026-07-29); the item stays on the list only because the *pair* re-test is still owed.
      (1) A `/24` ping sweep leaves an ARP entry for every address probed, and this router
      answers ARP for unused addresses: `ip neigh | grep -c lladdr` went 22 -> **259** across
      a sweep, decaying back to 22 about 20 s later. One background probe per entry meant
      ~250 concurrent wgets on a 4-core handheld inside a 4 s collect window. `nx_find_peer`
      now dedups by MAC before probing (measured 222 -> 16 candidates, peer included).
      Note busybox `sort -u -k1,1` compares WHOLE LINES and does not dedup by key — awk does it.
      (2) The decisive one: `nx_fetch` named its download `/tmp/nx_netplay_fetch.$$`, and in
      POSIX sh `$$` keeps the PARENT shell's pid inside a subshell — verified, parent and both
      subshell forms all printed 27146 — so every concurrent probe shared one file. One probe's
      trailing `rm -f` deletes another's payload (16 concurrent probes against a LAN with
      exactly one real peer returned **zero** hits), and a dead host's `cat` can read a live
      host's payload and win `ls | sort | head -1` (observed: 192.168.1.14, which refuses
      connections on 19714, beat the real peer). `nx_fetch` now uses `mktemp` with a
      URL-derived fallback. The two bugs mask each other: dedup alone cannot fix discovery,
      and the temp-file fix alone would drown under ~250 probes.
      The storm is **device-dependent**, and that is what explains why the client found the
      host but never the reverse. Same LAN, same moment, identical sweep: the Brick goes
      22 -> **259** neighbours, the Smart Pro S goes 3 -> **13**. So the Smart Pro S spawned
      few enough probes that the temp-file race still let a hit through occasionally, while
      the Brick spawned ~250 and never got one. (Mechanism behind 259-vs-13 not established —
      band/AP or neighbour-table GC — only that it is real and repeatable.) Practical
      consequence: **bench discovery on the Brick**; the Smart Pro S is not sensitive enough
      to expose this class of bug.
      Verification rig, reusable: run a peer on any same-subnet host with
      `printf 'GAME\nclient\n' > netplay-info && python3 -m http.server 19714`, extract the
      discovery functions from the deployed launch.sh with awk (one function at a time —
      overlapping `sed` ranges silently duplicate one-liner bodies like `nx_cancelled`),
      then call `nx_find_peer client`. Post-fix: 3/3 runs found the peer in 8 s each, on the
      same LAN that shows 259 phantom neighbours. Misses are still never cached.
- [ ] **Discovery: known limits accepted, NOT bugs to re-derive** — a review of the 2026-07-29
      discovery fix raised these; each was checked and consciously left alone. Read this before
      "fixing" any of them.
      (a) *MAC dedup can drop the real peer.* `awk '!seen[$1]++'` keeps one IP per MAC in kernel
      hash order. If the AP answers the sweep's ARP for the peer's own address with the AP's MAC
      and that reply wins the cache, the peer joins the AP's group and is only probed if it
      happens to be first in hash order — and it would fail identically every pass for the full
      90 s. Empirically the peer survived on both devices across 6 runs (last-reply-wins appears
      to favour the real station). No cheap complete fix exists: probing all ~222 in batches
      would eat ~64 s of the 90 s budget. Dedup trades completeness for load; at 222 candidates
      that is the right trade. If this ever does bite, the cheapest mitigation is appending the
      stored `server =` to the candidate list unconditionally.
      (b) *Candidates are not filtered to the local /24.* Verified: `10.0.0.5` on a second
      interface survives dedup and gets probed. Harmless waste on a normal LAN; left alone
      because filtering must be conditional on `NXP_NET` being non-empty or it would break
      discovery entirely on a non-/24 lease.
      (c) *`NETPLAY_DEADLINE=90` is a loop-ENTRY deadline.* Parallelising the probes dropped the
      per-candidate deadline check, so a pass starting at T+89 still runs its full ~8 s and the
      `$( )` then waits on stragglers. Real bound is ~90 + 8 + one probe.
      (d) *`$NXP_HITS` safety rests on an unstated timing margin.* A probe lives <=3 s; the gap
      from spawn to the next pass's `mkdir -p` is `sleep 4` + `sleep 3` = 7 s. **Invariant: the
      collect `sleep 4` must exceed `nx_fetch`'s watchdog argument.** Shorten one or raise the
      other and hits start crossing passes (worst case: one lost pass, ~8 s).
      (e) *The mktemp fallback is not collision-free in one dormant case* — the same IP on two
      interfaces with different MACs survives dedup twice and both fallback names are identical.
      Dormant because `mktemp` is present on both devices, and both probes target the same host
      so a stolen payload is the right payload.
      (f) *`ls | sort | head -1` is lexicographic, not numeric* (`.100` < `.20` < `.9`). Fine for
      the one-real-peer case; with two eligible peers the "same LAN picks the same peer"
      property is weaker than it sounds, since which IP wins each MAC slot is kernel hash order.
- [ ] **Two-device pair re-test** — the actual outstanding item: host on one device, client
      on the other, both on the busy `/24`. Host must reach "found player 2" rather than
      sitting out the 90 s deadline, and both must clear flycast's Starting Network modal.
- [ ] **python3 serve branch under the real launch env** — the tg5050 smoke test above
      ran from a bare adb shell; launch.sh prepends repo-built libs to `LD_LIBRARY_PATH`
      (tg5050 launch.sh:47). Confirm `python3 -u -m http.server` still starts and serves
      a fetchable `netplay-info` with that exact environment applied.
- [ ] **Overlay token after an unclean exit** — toggle GGPO on, then power-cut instead of
      quitting cleanly; confirm `emu.cfg` still holds `GGPO = yes` (not `True`). A `True`
      is the one state that would make launch.sh skip the netplay block entirely while
      flycast itself still enables GGPO — i.e. netplay with no discovery and no sync.
- [ ] **Filename-mismatch pairing** — the same CHD under two different filenames on the
      two cards: they must NOT pair (`netplay-info` line 1 is the extension-stripped ROM
      filename, not a content hash). Confirm the symptom stays thin: the status screen
      sits for the full 90 s on whichever message survived show2's init window — on
      the client "looking for the host...", its only pre-wait message; on the host
      EITHER "preparing saves..." OR "looking for player 2 on ..." — then simply
      disappears, since the "starting anyway" update is queued and killed before it
      can be read. Then
      flycast's untimed "Starting Network" — nothing names the mismatch.
- [ ] **Local two-pad regression** — with GGPO off, set `[input] device2 = 0` through
      flycast's OWN Controls UI for local two-pad play, relaunch with GGPO still off, and
      record that the restore branch reverts it to `10` (it cannot distinguish that value
      from one this block wrote).
- [ ] **Server cleanup on a pak kill** — quit through the OSD/power path rather than
      flycast's own exit, then `netstat -tln | grep 19714`: the cleanup block only runs
      after `wait $EMU_PID`, so a kill that bypasses it may leave the serve dir listening.
- [ ] **GGPODelay asymmetry** — set a different Input Delay on each side and confirm it is
      NOT a desync (`inputSize` depends on `GGPOAnalogAxes`, not on delay), so the "must
      match" warning stays correctly scoped to `GGPOAnalogAxes` and `device2`.
- [ ] **No stray listener** — after a normal quit, a crash, and a power-off mid-session:
      port 19714 must not be listening (`netstat -tln`); the serve dir is LAN-readable
      for as long as a server lives.
- [ ] **Dead/duplicate server** — launch either device with 19714 already bound (or
      double launch): launch.sh must fall open and reach flycast rather than stall.
      Burning the full 90 s deadline is NOT a failure — a device with no reachable
      peer always does; the assertion is that the launch proceeds. Note observed
      behavior. Both roles run a server now, so this applies to host and client alike.
- [ ] **Upgraded-install cfg creation** — on a real `.initialized` device cfg (no
      seeded `[network]`, and typically no `[input]` section at all): overlay toggle
      creates `[network]`, discovery creates `server =` from scratch, and the netplay
      block creates `[input] device2` from scratch (bench proved the mechanics on a
      live-shaped cfg, not the live file).
- [ ] **tg5050 tar guard on hardware** — re-run the five-condition guard cases under
      Smart Pro S busybox 1.35 tar (the hardlink/mode-column rendering is verified on
      the Brick's busybox 1.27.2 only).
- [ ] **Speaker pop on netplay launches** — unmute timer fires at T+5 s but flycast
      can start up to 90 s later; listen for the pop the mute normally suppresses.
- [ ] **DCNet + GGPO together** — README claims independence; confirm on-device or
      document mutual exclusion (DCNet's non-deterministic traffic would desync
      rollback).
- [ ] **Non-/24 LAN** — a device on a /16 or /23 lease: neighbor-only scan, clean
      fail-open, and no self-pairing with its own server (each device now serves one).
      Now true in a stronger sense: the address is parsed with a `/24`-only `sed`, so
      off a /24 the subnet comes back empty and the stored `server =` is skipped
      UNCONDITIONALLY, not just when the octets differ. Consequence to check: a
      hand-set `server =` is never probed by the FAST PATH there — the `ip neigh` loop
      still runs, so that host is reachable only if it already sits in the ARP cache,
      and nothing sweeps to populate that table off a /24. So the client may pull no
      save tar even though flycast still connects on that address (launch.sh leaves
      the value alone when discovery finds nothing) — GGPO connects, sync does not.
- [ ] **Status screen appears** — SETUP MATTERS: run this with the peer absent, or
      with `[network] server =` cleared on this device first, so the wait is long
      enough (90 s / >=7 s) to exercise the `TEXT:` update path properly. On the fast
      path discovery can outrun the screen and either a brief flash or nothing at all
      is correct — that is the item below, not this one. With this setup, PASS = all
      three of: (1) a netplay
      status screen appears; (2) the message on it is ONE OF the literal strings
      below; (3) the launch proceeds to flycast. ONE EXCEPTION, and it is a FAIL: if
      `Starting...` is the only thing ever shown for the whole wait, no
      `TEXT:` update reached the FIFO — that string is show2's own `--text=` argument,
      painted from the first frame whether or not the update path works at all (a
      failed `mkfifo` only `perror`s and keeps rendering, `show2.cpp:238-241`; an
      `nx_ui` subshell that exhausts its 10 s poll budget exits silently,
      `launch.sh:229`). This is the only item that exercises the `TEXT:`/`PROGRESS:`
      path *from launch.sh*, on either device (the deployed tg5050 show2 has accepted
      `TEXT:`/`PROGRESS:` by hand, but nothing has driven it through a real launch
      there). It cannot false-fail a correct run:
      with the setup above the wait is >=7 s (cleared `server =`) or 90 s (peer
      absent), against a 1 s poll and a 10 s budget. Otherwise do NOT require a
      particular message, or any particular sequence — the design guarantees neither.
      Expected survivors,
      by role, quoted exactly as launch.sh emits them (the `Netplay: ` prefix these
      strings once carried was dropped 2026-07-29 after it read as clipped on hardware —
      a screen still showing it is running an old launch.sh): CLIENT — `Looking for
      the host...` (it queues only one pre-wait message). HOST — EITHER `Preparing
      saves...` OR `Looking for player 2 on <subnet>.x...`, which
      off a /24 reads `Looking for player 2 on the network...`. Sent but
      rarely seen: `Starting...` (the daemon's own `--text=`), `Player 2 found at
      <ip>`, `No player 2 found - starting anyway`,
      `No host found - starting anyway`. The one terminal message that IS
      readable is the client's `Host found at <ip> - copying saves...`,
      which dwells ~1-2 s (the tar fetch of a ~384 KB card over LAN, plus a few `tar`
      spawns; the 30 s in `nx_fetch` is only the watchdog ceiling for a stalled host).
      Why the chain is not assertable: (a) each `nx_ui` call polls for the FIFO from
      its OWN background subshell on an independent `sleep 1` cadence, so among the
      messages queued before the FIFO exists ANY of them may be the survivor — on the
      host it can be "preparing saves..." or "looking for player 2 on ...", either
      way; (b) terminal messages are only queued, and the very next thing launch.sh
      does is `nx_ui_stop`, whose `killall -9` destroys the daemon within
      milliseconds — at 60 fps that is ~17 ms (derived, not measured) to paint a frame
      that is then torn down.
- [ ] **Fast path — brief screen or none, BOTH correct** — the counterpart to the item
      above, worth recording deliberately because it is the state a tester will be in
      by default (every preceding pair test auto-writes `server =` on both devices).
      Relaunch with a valid stored `server =` and the peer up: the stored probe returns
      in ~1 s (`nx_fetch` checks `kill -0` BEFORE its first `sleep 1`), so the whole
      block finishes in roughly 1.5-2.5 s on the host and 2.5-3 s on the client. With
      init measured at <=1 s the screen will PROBABLY paint briefly and then vanish —
      but a run where it never appears at all is equally correct. PASS = the launch is
      fast and reaches flycast; do NOT fail it either way. (This was written when init
      was estimated at ~3-4 s and predicted a guaranteed black screen; the measurement
      inverted that prediction, hence the either-outcome wording.) The insight the item
      exists to record is unchanged: the faster discovery gets, the less the status
      screen has to show, because there is less wait left to cover. Use the peerless /
      cleared-`server =` setup of the item above — not this one — to actually exercise
      the `TEXT:` update path.
- [ ] **Status screen never outlives the launch** — after flycast starts, confirm no
      `show2.elf` process remains. Use `pgrep -x show2.elf`. NOT `pgrep -f show2` and
      NOT a bare `ps | grep show2`: both match the checking shell's own command line
      and report a phantom hit (this actually fooled a bench session). Check on both
      roles. Cleanup depends on `killall -9` — see the Gotchas entry; if a plain
      SIGTERM ever gets restored here, this item is what catches it.
- [ ] **Degrades silently** — rename `show2.elf`, relaunch: netplay must still work,
      just without the status screen, and must not stall.
- [ ] **Relocation is fast** — join a different network so the stored `server =`
      is on the old subnet; confirm the stale address is skipped (no ~3 s dead
      probe) and discovery still finds the peer.
- [ ] **Busy-LAN scan time** — on the 60-AP location, time ONE discovery pass, not a
      full failed scan: a failed scan is deadline-bound at 90 s under serial and
      concurrent probing alike, so it cannot tell the two apart. A pass is a fixed
      ~7 s (3 s sweep settle + 4 s collect) plus the slowest straggler probe, each
      bounded by the 3 s fetch watchdog — and crucially that figure must NOT grow
      with the neighbour count. Count the neighbours (`ip neigh | wc -l`) so the
      "does not scale" claim has a number attached.
- [ ] **B skips netplay** — wait until the netplay screen is actually up and the
      "B = skip netplay" hint is visible, THEN press B (pressing during show2's
      own startup still cancels, but the confirmation cannot be drawn yet because
      its FIFO does not exist). The screen should change to
      `Skipped - starting game...` and the game should start
      single-player within a couple of seconds. Afterwards confirm
      `[network] GGPO` is STILL `yes` in emu.cfg — the skip is per-run only.
- [ ] **Cancelled client uses its OWN saves** — cancel on the client role, then
      check in-game that the VMU content is the client's own, not the host's
      borrowed copy (netplay isolation is skipped when cancelled).
- [ ] **B works outside the search too** — press B while the host is preparing
      saves, and (separately) while the client is copying saves: both must skip.
- [ ] **Only B cancels** — press A, X, Y, START and move the stick during the
      search: none may skip netplay.
- [x] **B is button `00` on BOTH units** — RESOLVED 2026-07-29 on-device: five
      consecutive presses on the Smart Pro S read `.. .. .. .. 01 00 01 00`,
      identical to the Brick, so the shared `NX_CANCEL_BTN=00` is correct for both
      and the byte-identical block needs no per-device handling. Keep the method
      note if this is ever re-measured: read js0 the way launch.sh does — open once,
      then `dd bs=8 count=1 <&3 | hexdump`. A buffered `dd | hexdump | while read`
      pipeline gives WRONG numbers, and that is exactly how the discarded "B = 02"
      reading was produced — do not "correct" `00` back to it.
- [ ] **Hint is legible** — both lines visible and unclipped, hint stays put while
      the status line changes. (Both cards were updated to the `--hint`-capable
      show2.elf on 2026-07-29, so a missing hint line means something other than an
      old binary — check the deploy first anyway with `md5sum`: 4be3ac43 on the
      Brick, c943d4b7 on the Smart Pro S.)
- [ ] **Leftover input reader clears itself** — on a NON-cancelled netplay launch
      the reader's `dd`/`hexdump` children outlive `nx_cancel_disarm` by design.
      Find them with `readlink /proc/*/fd/*` matching `/dev/input/js0` — NOT by
      cmdline, since the path is a redirection and never appears in argv. Just
      after the game starts they may still be listed; press any controller button
      and re-check: they must be gone, because `dd` ran with `count=1`. Confirm
      in-game input is unaffected while they linger.
- [ ] **Install splash unaffected** — the deployed show2.elf is also the first-boot
      splash; run `install/boot.sh`'s invocation (no `--hint`) and confirm it
      renders as before. Bench-passed pre-deploy on the Brick with boot.sh's exact
      argument list (starts, FIFO in 1 s, accepts `TEXT:`, log identical to the
      baseline); this item is the same check against the binary now on the card, on
      both devices. If it ever fails, `show2.elf.orig` sits beside it on-device.

### Gotchas

- `GGPO = yes` persists across quits — every subsequent launch of ANY DC game waits for a
  netplay peer until it's toggled back off in the overlay. Turn it off after playing.
  Pressing B to skip does NOT count as turning it off: it disables netplay for that one
  run through a virtual `-config` value flycast never writes to `emu.cfg`, so the next
  launch searches again — and `[input] device2` stays at `0` until a launch with GGPO
  actually off puts it back to `10`.
- The peer IP (`server =`) is auto-written by launch.sh discovery on both sides, and
  is probed first on the next launch (fast path) — but ONLY when the device holds a
  /24 lease AND the stored address is inside it. Off-subnet (after a relocation) or
  on any non-/24 lease it is skipped entirely, which is what makes those launches
  fast instead of costing a dead ~3 s probe. Manual emu.cfg edit is a fallback for
  LANs where the /24 sweep + neighbor probe can't find the peer, with the same
  caveat: on a non-/24 lease a hand-set value is never probed by the fast path (the
  `ip neigh` loop still runs, but nothing sweeps to populate that table off a /24,
  so it only helps if the peer is already in the ARP cache), and flycast may then
  connect on that address while the client still pulls no save tar.
- `GGPO = yes` persisting (previous gotcha) now also means the discovery/sync phase
  (up to 90 s) runs on EVERY DC launch until it's toggled off — one more reason
  to turn it off after playing. It also means `[input] device2` stays at `0`
  (port B occupied) until a launch with GGPO off puts it back to `10`.
- Netplay launches must be plain "A START", never switcher-resume (slot-9 load after
  GGPO's boot-time start desyncs; the client's slot 9 lives in `netplay-data/` so
  normal resume is never polluted, but the host's real slot 9 is).
- **show2 must be killed with `killall -9`, never plain `killall`.** Measured on the
  Brick 2026-07-29: SIGTERM leaves it alive (still running 6 s later); `-9` kills it
  instantly. show2 installs a handler for SIGINT only (`show2.cpp:623`), and
  `SDL_Init` traps SIGTERM into an `SDL_QUIT` event that show2's daemon loop never
  pumps — so the signal is swallowed. `nx_ui_stop` uses `-9` for this reason, as does
  `MinUI.pak/launch.sh:228`. Anything that "tidies" it back to a plain `killall`
  leaves the status screen holding the display against flycast.

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

---

## Game Switcher resumable filter + standalone-emulator resume

**Status:** merged to `main` 2026-07-26 (`13a4d3c4`, PR #54). Verified on Brick and
Smart Pro S (filter, setting, N64 resume handshake, fresh-launch cold boot). Only the
Brick Pro pass remains.

Two related changes:

1. **Switcher filter** — the Game Switcher lists only games with a resumable save state
   by default; Settings → System → "Game Switcher games" toggles between "Resumable only"
   and "All recent games" (config key `switcherresumableonly`). Non-resumable entries in
   "All" mode show `A START` instead of `A RESUME`.
2. **Standalone resume handshake** — nextui writes the slot to `/tmp/resume_slot.txt` on
   every launch; minarch consumes it in `State_resume()`. The N64 pak (mupen64plus +
   GLideN64 overlay) now consumes it too: `emu_ovl_consume_resume_slot()`
   (`emu_overlay.c`) is called once from `DisplayWindow::swapBuffers` after overlay init
   and auto-loads slots 0-7 via `M64CMD_STATE_SET_SLOT` + `M64CMD_STATE_LOAD`. Slots 8/9
   (fresh launch / minarch auto-resume) are ignored by design.

### On-device verification

- [ ] **Brick Pro (pending hardware)** — switcher filter + setting, and N64 resume if the
      pak gains a tg4040 build.

### Standalone emulators still without resume

The repo ships exactly three standalone (non-minarch) emulator paks; everything else in
`skeleton/EXTRAS/Emus/` launches `minarch.elf` and already resumes via `State_resume()`:

| Pak | Emulator | Resume status |
|---|---|---|
| N64 | mupen64plus + GLideN64 overlay | **Works** (this change) |
| DC | flycast + nx_overlay | **Works**, incl. auto-save-on-quit to hidden slot 9 (`/tmp/resume_slot.txt`, same handshake as N64). Merged `99985dec` (PR #56), verified on Brick + Smart Pro S. Still pending: full RA session test (a live achievement unlock, not just login/HTTPS), and Brick Pro once hardware arrives. Details: `workspace/all/other/flycast/README.md`. |
| NDS | DraStic (closed-source binary) | **No resume, by design.** No overlay integration and no `.minui/` slot files, so NDS games are hidden by the resumable-only filter and show `A START` in "All" mode — honest behavior. Baking resume in would need DraStic's own savestate CLI/auto-load hooks, if any exist; the emu_overlay approach is not available without source. |

User-installed paks that are not part of this repo (e.g. a community PSP/PPSSPP pak) are
in the same position as NDS unless they write `.minui/<EMU>/<rom>.txt` slot files — if one
does, it must also consume `/tmp/resume_slot.txt` or the switcher's RESUME promise will be
cosmetic (exactly the bug fixed for N64).

### Gotchas

- The GLideN64 build needs three aarch64 static libs recreated after applying the patch
  (bundled ones are x86-64; the patch records them content-lessly). Recipe in
  `workspace/all/other/mupen64plus/README.md` — note especially that `libz.a` must be
  a self-built zlib ≥1.2.9, NOT the sysroot's (sysroot zlib lacks `adler32_z` → the .so
  builds fine but fails `dlopen` at runtime with a blank screen).
- mupen64plus patches + docs live in `workspace/all/other/mupen64plus/` (deduped
  2026-07-26; per-platform dirs keep only the gitignored source checkouts). The GLideN64
  patch is regenerated against the pinned `GLIDEN64_COMMIT` in the platform Makefile —
  regenerate against that pin, not upstream master.

---

## Trimui Brick Pro (tg4040)

**Status:** ported 2026-07-25, merged to `main` 2026-07-26 (`c0da09c7`, PR #53). Never run
on hardware — device ordered 2026-07-25.

Ported from upstream NextUI [PR #766](https://github.com/LoveRetro/NextUI/pull/766) plus five
follow-up commits that fix bugs in it: `9dffb9e8` (L4/R4 remapping), `d47fb074` (`MAX_LIGHTS` 5),
`ff202893` (rumble voltage cap), `a20481b7` (mute-buzz voltage), `bade2a41` (minput R-group
layout regression).

### Build and flash

```bash
make all                       # produces releases/NextUI-<date>[-branch]-brickpro.zip
```

Copy the `-brickpro` zip to the SD card as `MinUI.zip` and boot, or for iterating on a single
binary use `make deploy DEVICE=brickpro` (pushes `MinUI.zip` over adb and reboots).

### Already verified — do not re-derive

Confirmed by reading the stock rootfs out of
`sd_recovery_tg4040_brick_pro_v1.1.1_20260717.img` (ext4 at sector 126432; recipe in `OSD.md`):

- `TRIMUI_MODEL` is exactly `Trimui Brick Pro` → `DEVICE=brickpro`
- LED zones are `f1 f2 m lr rear`; brightness nodes `max_scale`, `max_scale_f1f2`,
  `max_scale_lr`, `max_scale_rear` (see the table in `INPUT_MAPPING.md`)
- `/usr/trimui/bin/trimui_inputd` exists, so the tg5040 boot path works unchanged, and its
  turbo interface is the same `/tmp/trimui_inputd/turbo_*` flag files
- the Smart Pro's analog-pad GPIO poke (PD14/PD18) is **commented out** in the Brick Pro's own
  `runtrimui.sh` — its sticks need no GPIO setup
- `trimui_osdd` is a distinct build from the Brick's, which is why the assembled
  `osd-brickpro/` overlay (built from `skeleton/SYSTEM/osd/device/brickpro/` at
  package time) exists

Confirmed live over adb on the actual unit, stock firmware v1.1.1 / kernel 4.9.191
sun50iw10, 2026-07-29 (device arrived, no SD card, redux not installed):

- `strings /usr/trimui/bin/MainUI | grep ^Trimui` → exactly one line, `Trimui Brick Pro`
- panel live at 1024×768@60 (disp sysfs + fbset); `overlay` in `/proc/filesystems`
  (kernel 4.9 → the tg5040 tmpfs-staging OSD mount branch is the right one here too)
- `/sys/class/led_anim/` has `effect_{f1,f2,m,lr,rear}` + `max_scale`,
  `max_scale_f1f2`, `max_scale_lr`, `max_scale_rear` exactly as tabled — plus
  standalone `effect_l` / `effect_r` (and matching rgb_hex/cycles/duration nodes),
  so the two `lr` sides are individually addressable (23 RGB LEDs in
  `/sys/class/leds/`); possible future refinement, not needed for bring-up
- `trimui_inputd` and `keymon` running; `trimui_osdd` runs from
  `/usr/trimui/osd/trimui_osdd` (the overlay mount point); turbo interface is the
  same `/tmp/trimui_inputd/turbo_*` flag files (per its own `help` file)
- PD14/PD18 analog-pad GPIO poke confirmed commented out in the LIVE
  `runtrimui.sh` (not just the recovery image)
- busybox v1.27.2 — same as the Brick (tar guard / applet findings carry over)
- **`TRIMUI Player1` key bitmap decodes to exactly the expected SDL order**:
  BTN range 304-318 (A,B,X,Y,TL,TR,SELECT,START,MODE,THUMBL,THUMBR → SDL 0-10),
  then low keys KEY_F1(59), KEY_F2(60), VOLDOWN(114), VOLUP(115),
  KEY_HOMEPAGE(172) → SDL 11-15. That is 8=MENU, 9/10=L3/R3, 11/12=FN1/FN2,
  13/14=volume, 15=HOME — kernel-level evidence for the Input Tester item below
  (still confirm in SDL once redux is installed). ABS=3003f → both sticks,
  analog triggers, dpad hat all present on the one device. HOME is
  KEY_HOMEPAGE(172) *emitted by the gamepad device*, so SDL sees it as joystick
  button 15 and keymon's tg5050 keyboard-device Home path indeed does not apply.
- `/dev/ttyAS*` — NONE exist (see resolved calibration item below)

### 1. On-device verification (Brick Pro)

- [ ] **Boots and identifies correctly** — UI is 1024×768 at 3× scale with 7 main rows.
      Confirm `DEVICE=brickpro` (not `smartpro`); a mis-detect shows up as a 1280×720 layout.
- [ ] **SDL joystick indices** — *the main unverified assumption.* Open
      Settings → Input Tester and press everything. Expected: 9/10 = stick clicks (L3/R3),
      11/12 = FN1/FN2 (shown as L4/R4), 13/14 = volume, 15 = HOME, 8 = MENU.
      Wrong indices look like dead or swapped buttons, **not** a crash.
- [ ] **Analog sticks** — both nubs move the on-screen indicators; `L3+R3` enters calibration.
- [x] **Hall-stick calibration** — RESOLVED 2026-07-29 on-device (stock fw): no
      `/dev/ttyAS*` nodes exist at all, so NX's calibration (`settings_input.c:68-71`,
      entered via `L3+R3` in the Input Tester) is a no-op on this model. Stock DOES
      calibrate here, via a different transport with the same downstream contract:
      the Brick Pro's sticks are read by `trimui_inputd` over I2C (`/dev/i2c-3`), and
      stock MainUI's flow is touch `/tmp/joypad_testmode` (raw/uncalibrated ADC mode)
      → sample → write the SAME `/mnt/UDISK/joypad.config` /
      `joypad_right.config` files NX already writes → touch
      `/tmp/trimui_inputd/cal_update` to make inputd reload ("calibrate update"
      string). DECIDED 2026-07-29: implement a native brickpro calibration on this
      mechanism. Same-day probe session reverse-engineered and hardware-verified the
      full acquisition protocol (raw ADC does NOT pass through the event device —
      the flag QUIESCES inputd; the calibrator reads the two stick ADC chips
      directly: `/dev/i2c-3`, 7-bit 0x28/0x29, reg 0xB0, 4 bytes = X,Y u16 LE,
      12-bit) — full protocol + design in `DEV_TODO.md` ("Trimui Brick Pro: joystick
      calibration"). Until it lands, `L3+R3` on brickpro is known-inert. Note the
      factory unit shipped with `joypad.config` present but NO `joypad_right.config`.
- [ ] **LEDs, all five zones** — Settings → LED Control shows F1 key / F2 key / Top bar /
      Joysticks / L/R triggers. Verify each zone lights the part it names (in particular that
      `lr` is the *joysticks* here and `rear` is the *triggers* — the opposite of the Brick).
- [ ] **LED brightness coupling** — F1 and F2 track each other (shared `max_scale_f1f2`);
      the other three are independent.
- [ ] **Per-zone effect lists** — the code picks by node name (`lr` gets the extended LR
      effects, `rear` gets the standard set). If an effect renders wrong or does nothing,
      adjust the selection in `settings_led.c` (`led_page_create`).
- [ ] **OSD** — long-press `MENU` opens it. Check the background/tile layout at 1024×768, that
      toasts land on-screen, and that the battery widget works (stock layout implies it does).
- [ ] **OSD stock restore** — overlay in `/proc/filesystems`; OSD overlay mount present after
      boot; Settings → Restore stock OSD round-trip (restore, verify rootfs matches
      `skeleton/SYSTEM/osd-stock/brickpro.manifest.md5`, reboot, NX OSD returns)
- [ ] **Rumble** — capped at 2.5 V; confirm it isn't unpleasantly strong at max.
- [ ] **Mute toggle buzz** — the FN-switch mute pulse uses 900000 µV on this model.
- [ ] **Backlight** — brightness ladder uses the Brick curve; check the low end isn't black.
- [ ] **HOME button** — maps to `BTN_HOME`, currently inert (matching Smart Pro S). Decide
      whether it should *do* something here; if so, note that it arrives as a gamepad button
      (index 15), not `KEY_HOMEPAGE`, so keymon's tg5050 Home path does not apply.

### 2. Regression checks (Brick / Smart Pro / Smart Pro S)

Shared code moved, so these need a pass on at least one older device:

- [ ] **Input Tester shoulder rendering** — L1/L2 and R2/R1 pills must look exactly as before.
      This is precisely what upstream broke and had to fix in `bade2a41`.
- [ ] **`pak.cfg` bind round-trip** — bind a shortcut in a game, restart minarch, confirm it
      survived. `BTN_ID_L4`/`BTN_ID_R4` were inserted mid-enum and `LOCAL_BUTTON_COUNT` went
      16 → 18; bindings persist by *name*, so this should hold, but it is the one change that
      could silently corrupt existing configs.
- [ ] **LED page** — Brick still shows 4 zones, Smart Pro/S still 3, and existing
      `ledsettings*.txt` files still parse after the `MAX_LIGHTS` 4 → 5 bump.
- [ ] **Brick Pro OSD background is now the Brick's.** `bg.png` moved into
      `res/<WxH>/` (it is exactly panel-sized, so it is resolution-locked art).
      The 1280×720 pair was byte-identical, so Smart Pro / Smart Pro S are
      unaffected. Brick Pro's stock version differed in 192 of 786,432 pixels:
      54 are ±1 alpha rounding on the panel corners (y≈56–80, invisible), and
      the other **138** are a teal accent (`0,255,163`) mirrored at x=41 and
      x=982, y≈686–711 — a 28 px fully-opaque core plus 110 px of anti-aliased
      edge. Brick's background is plain black there, so Brick Pro loses both
      accents. Judged negligible while the hardware is
      unavailable — look at it once a Brick Pro is in hand and restore
      `device/brickpro/bg.png` if the accents matter.

### 3. Deliberately deferred

Three items were scoped out of the port and are tracked in `DEV_TODO.md`: the PortMaster
device entry, display calibration / white point, and the wrongly-sized 1024×768 music
widget tile. None of them block bring-up.

### Gotchas

- OSD is overlay-mounted read-only at boot — from the SD directly on tg5050,
  via a tmpfs staging copy on tg5040 (its kernel 4.9 overlayfs rejects exFAT
  as a lower layer); no rootfs writes and no stamp on either. If the OSD looks
  stale or dead, check `/proc/mounts` for `/usr/trimui/osd` and
  `/tmp/nx_osd_mount_failed`.
- OSD assets are layered (`common/`, `res/<WxH>/`, `device/<dev>/`) and assembled
  by `scripts/assemble-osd.sh` at package time — edit the layer, not a device tree.
- `make deploy` now takes `DEVICE=` (e.g. `make deploy DEVICE=brickpro`). Passing
  only `PLATFORM=tg5040` deploys `brick`.
- Don't push an `.elf` over a running copy — stop the pak first. Only `nextui`/`minarch`
  need a reboot after pushing; other paks just need to not be running.
- Never `killall nextui` on device: the `kill -9` path powers the unit off.

---

## Thread-pinning `taskset` now actually works — re-verify everything that uses it

**Status:** fixed and merged to `main` 2026-07-27 (`99985dec`, PR #56 — task 11 fix round).
tg5050 (Smart Pro S) fully verified (native `taskset`, PS.pak probe, DC.pak + N64.pak
pinning — evidence in `.superpowers/sdd/2026-07-26-flycast-dc-pak/n64-tg5050-report.md`).
Only the Brick N64.pak re-verify remains.

`skeleton/SYSTEM/shared/bin/taskset` — the binary every pak's `taskset` calls resolved
to via `PATH` — was a `-static` build that aborted with `FATAL: kernel too old` on the
Brick's real 4.9.191 kernel. Every call site wraps `taskset` in `2>/dev/null`, so this
failure was completely silent: **every existing thread-pinning call in the repo has
been a no-op on tg5040 this whole time**, not just for DC.pak. Fixed by dropping
`-static` from `workspace/all/taskset/Makefile` and shipping working, platform-specific,
dynamically-linked binaries at `skeleton/SYSTEM/{tg5040,tg5050}/bin/taskset` (which
shadow the old shared path via existing `PATH` ordering — no call-site changes needed).
The old shared binary was deleted this round, so **there is no fallback anymore** if a
platform's `taskset` turns out to be broken on some device.

- [ ] **N64.pak pinning on Brick, with pinning actually active** — re-verify audio/perf
      with real affinity applied. The masks and the thread-name heuristic
      (`skeleton/EXTRAS/Emus/tg5040/N64.pak/launch.sh:100,108,127`) were written and
      shipped blind, against a `taskset` that always silently failed; they were never
      exercised for real until this fix, the same way DC.pak's pinning was
      evidence-gated by direct measurement (task-11 report) before shipping.
      **Known gap, measured on Smart Pro S 2026-07-27 (reproduced twice, incl. a real
      user session):** the "pin the busiest `mupen64plus`-named thread" heuristic only
      pins ONE of the (at least) two non-main threads named `mupen64plus`; the other is
      left on the unrestricted 0-7 mask. Measured impact is small: its load is bursty
      init/loading work (~3.6% during boot, ~0% in live gameplay), and since NextUI only
      brings cpu0-1/4(/5) online (8-core silicon run as effective 4-core by boot policy),
      "unrestricted" still lands it on the contended cores. The fix (pin unmatched threads
      to LITTLE by default) is written up in `DEV_TODO.md` and should land with this
      re-verify, so it ships measured rather than blind.
