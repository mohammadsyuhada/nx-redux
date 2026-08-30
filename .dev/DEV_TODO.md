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

## Updater: drop api.github.com to escape the 60/hr rate limit

**Recorded:** 2026-08-03, after a live "Failed to check for updates" on Brick Pro
(v1.4.1) turned out to be **HTTP 403 rate limit exceeded**, not a code fault.

`auto_check_thread` (`workspace/all/settings/settings_updater.c:276-309`) fetches
`https://api.github.com/repos/mohammadsyuhada/nx-redux/releases/latest`. The
**unauthenticated** GitHub API is capped at **60 requests/hour per public IP**, and
that budget is shared across every device behind the same NAT — so a household or
office of Redux devices (plus any dev machine hitting the same repo) exhausts it
network-wide, and every device then shows the generic failure until the hourly
window resets. Confirmed on device: wget got `403 rate limit exceeded`,
`X-RateLimit-Remaining: 0`, exits 8, `wget_fetch` returns -1 →
`settings_updater.c:295` "Failed to check for updates".

**The fix:** get the latest tag from the web endpoint
`https://github.com/mohammadsyuhada/nx-redux/releases/latest`, which 302-redirects
to `/releases/tag/vX.Y.Z` and is **not** subject to the api.github.com 60/hr limit.
Read the tag from the redirect `Location:` (`wget -S --max-redirect=0`, parse the
`Location:` header — the same `-S` header-parsing path `wget_download_file` already
uses at `wget_fetch.c:152-174`), then build the asset download URL from the
predictable pattern
`https://github.com/<owner>/<repo>/releases/download/<TAG>/…-<device>.zip`, where
`<device>` is today's `get_device_name()` mapping (`settings_updater.c:43-52`) — the
same suffix `find_zip_asset_url` matches now.

- [ ] Replace the api.github.com JSON fetch with the github.com redirect + tag parse;
      derive the asset URL from the tag instead of scanning the `assets` JSON.
- [ ] Decide the exact download asset filename convention and hardcode it (the JSON
      `browser_download_url` scan goes away, so the name has to be reconstructed —
      confirm it against what the release workflow actually uploads per device).

**Tradeoffs / what's lost.** The web redirect returns only the tag, so the release
**notes body** (`find_json_string(... "body" ...)`, shown by `show_update_info`) and
the JSON asset list disappear. Either drop the notes UI or fetch the body separately
(a second call that *would* re-touch the rate-limited API — defeats the point). The
`target_commitish` SHA (`settings_updater.c:330`) is likewise unavailable from the
redirect; the tag-only comparison at `settings_updater.c:347-351` still works, so
this is fine as long as nothing downstream needs the SHA.

**Consider while here (option 1, not this task):** even keeping the API, the error
message is identical for no-internet / DNS-fail / 404 / rate-limit because
`wget_fetch` runs with `-q` and discards the HTTP status. Surfacing the status (drop
`-q`, parse `-S`) so the updater can say "GitHub rate limit — try again shortly" is a
smaller, independent win. This entry is the larger structural fix that removes the
limit entirely.

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
