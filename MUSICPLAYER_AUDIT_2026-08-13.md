# Music Player Full Scan — 2026-08-13

Full audit of `workspace/all/musicplayer/` (~23.5k LOC app code, vendored libs excluded), run as five
subsystem reviews (playback core, radio, podcast, module/navigation, UI/metadata). Every HIGH was
re-verified against the source before landing here. Line numbers are as of commit range around the
context-menu + shuffle-fix work (2026-08-12/13) and will drift as fixes land.

Legend: `[ ]` open · `[x]` fixed · `[-]` won't fix / accepted.

Fix batches agreed:
1. **Batch 1**: PB-H1, PB-H2, MOD-H1, MOD-M1 (small + surgical)
2. **Batch 2**: radio network HIGHs (RAD-H1..H3)
3. **Batch 3**: podcast locking/corruption set (largest rework)
Remaining items to be scheduled after.

---

## HIGH

### Playback core

- [x] **PB-H1 — Decode thread busy-spins at 100% CPU at EOF** — `player.c:1207-1243` (`stream_thread_func`)
  `usleep(5000)` only fires in the buffer-full branch. Once `stream_decoder_read()` returns 0, the loop
  sets `stream_eof` and iterates with no sleep: buffer below half (draining or drained) + EOF = spin.
  Spins from decode-finish until buffer drains on every track; spins indefinitely if playback ends and
  the user stays on Now Playing. Battery/thermal damage on a handheld.
  **Fix**: sleep when `decoded == 0`. *(fixed in batch 1)*

- [x] **PB-H2 — Audio-sink hotplug with NULL resampler silences the track and fast-forwards it** —
  `player.c:2041-2051` (creation), `reopen_audio_device()` `player.c:1492-1551`, drop at `player.c:1116-1122`
  Resampler is created only in `load_streaming()` and only if `src_rate != dst_rate` at load time.
  `reopen_audio_device()` (BT/USB-DAC hotplug) changes `current_sample_rate` but never creates one;
  `src_process(NULL)` → `SRC_ERR_BAD_STATE` → `resample_chunk` returns 0 → every decoded chunk dropped:
  silence while `current_frame` races to end (combined with PB-H1's spin).
  **Fix**: ensure/recreate the resampler after reopen when rates differ. *(fixed in batch 1)*

### Radio

- [x] **RAD-H1 — ICY metadata straddling a recv boundary permanently desyncs the stream** — `radio.c:1128-1132`
  When `meta_len > 0` but `i + meta_len > bytes_read`, the block is neither parsed nor skipped: partial
  metadata bytes go into the audio buffer and `bytes_until_meta` resets to a wrong offset. With
  metaint 16000 and ≤8KB recvs, any song-change block crossing a boundary feeds title text to the
  decoder (glitch) and desyncs all later metadata until reconnect.
  **Fixed (batch 2)**: added `icy_meta_buf[4080]`/`icy_meta_len`/`icy_meta_got` to RadioContext; the
  stream loop now collects a metadata block across recvs and parses it whole (unifying the fully-present
  and straddling cases). Reset alongside `bytes_until_meta`.

- [x] **RAD-H2 — ID3v2.3 frame-size integer overflow → huge memcpy on untrusted HLS data** —
  `radio_hls.c:229-248` (TIT2), same for TPE1 `:252-261`
  For version < 4 the raw 32-bit `frame_size` check `pos + 10 + frame_size > (uint32_t)total_size` wraps
  for values near 2^32 and passes; `int text_len = frame_size - 1` goes negative and `memcpy` receives a
  ~2^32 `size_t`. One corrupt/malicious segment crashes the app.
  **Fixed (batch 2)**: check rewritten as `frame_size > (uint32_t)(total_size - pos - 10)` — no addition
  to wrap; the loop guard `pos + 10 < total_size` keeps the subtraction positive.

- [x] **RAD-H3 — Direct HTTPS connect has no socket timeout and runs on the UI thread** —
  `radio.c:397-434` + `parse_headers` `radio.c:512-525`; same class: `radio_net_resolve_url` HTTP hop
  `radio_net.c:768-791/:826`
  No `SO_RCVTIMEO` on the mbedTLS fd (unlike `radio_net.c:199-201`); `Radio_play()` runs on the main
  loop. Stalled Wi-Fi after TCP connect → handshake/header read blocks for minutes → UI frozen,
  `Radio_stop()` can't help (same thread).
  **Fixed (batch 2, mitigation)**: new `tcp_connect_timeout()` (non-blocking connect + `select` +
  `SO_RCVTIMEO`/`SO_SNDTIMEO`) used for both the HTTPS and HTTP `connect_stream` paths; handshake bounded
  by a 15s wall-clock deadline (+ `should_stop`); recv/send timeouts added to `radio_net_resolve_url`'s
  plain-HTTP hop.
  **Fully fixed (follow-up)**: the entire connect phase now runs on the stream thread. `Radio_play`
  detects HLS-vs-direct (cheap), resets buffers, creates one `radio_stream_thread_func` worker and
  returns immediately in `CONNECTING` (the UI already renders that state). The worker calls
  `hls_connect()`/`direct_connect()` (extracted from the old `Radio_play`: resolve, redirects, HLS
  playlist fetch, TLS handshake, header parse) then tail-calls the existing `hls_stream_thread_func` /
  `stream_thread_func` loop. The UI never blocks on radio network I/O. `tcp_connect_timeout` now polls
  `should_stop` in 200ms slices and the redirect loop checks it, so `Radio_stop` (station switch / back)
  aborts a dead connect promptly. `thread_running` is cleared on `pthread_create` failure (also closes
  RAD-M3 for this path). Residual: `radio_net_fetch`/`radio_net_resolve_url` (the resolve hop) aren't
  `should_stop`-aware, so aborting mid-fetch waits up to their ~10s timeout — bounded, not a freeze;
  full abort-ability tracked with IMP-4/IMP-5.

### Podcast

- [x] **POD-H1 — Download thread works on the live queue with no lock; main thread compacts it under lock** —
  `podcast.c:1806-1901` (thread, no `download_mutex`) vs `Podcast_cancelEpisodeDownload`
  `podcast.c:1695-1698`, `Podcast_unsubscribe` `podcast.c:904-916`, `Podcast_loadDownloadQueue`
  `podcast.c:2004-2053`
  Cancel-active-download → array shift-compacts while the thread holds `item` → status/progress land on
  the wrong row and `unlink(item->local_path)` (`podcast.c:1884`) can delete the wrong episode's file.
  **Fixed (batch 3)**: `download_thread_func` restructured — under the lock it finds the next PENDING
  item and copies its identity (guid/url/local_path/titles) to locals; it never holds an array pointer or
  passes `&item->field` across the blocking wget. Live progress goes to a new stable global
  (`download_progress.current_percent`/`current_guid`, single writer); the queue view and
  `Podcast_getEpisodeDownloadStatus` read the active row's progress from there (guid-matched). Final
  status is written back by re-locating the item by guid under the lock, and `unlink()` only ever targets
  the thread's **local** `local_path` copy — so the wrong-file deletion is impossible. Folds in POD-M3
  and UI-M2 (no more lock-free reads of a rebinding row for the cancel target / live progress).

- [x] **POD-H2 — Refresh thread holds `&subscriptions[index]` across network I/O; unsubscribe shifts the array** —
  `podcast.c:969-1068` (`Podcast_refreshFeed` from `refresh_thread_func` `podcast.c:2105-2121`),
  unsubscribe shift `podcast.c:933-941`
  Refresh-all in flight + unsubscribe of a lower index → another feed's title/author/episode_count and
  `episodes.json` get overwritten with the refreshed feed's data. Persistent cross-feed corruption.
  **Fixed (batch 3)**: `Podcast_refreshFeed` captures the feed's `feed_url` under the lock up front, uses
  the `feed_url`-derived `feed_id` (via `set_feed_id(&temp_feed)`) for all on-disk paths, and re-resolves
  the subscription by `feed_url` under the lock before writing metadata back — aborting cleanly if the
  feed was unsubscribed mid-refresh. Extracted `save_episodes_by_id()` (array-independent) so
  `episodes.json` is always written under the right feed_id; `Podcast_saveEpisodes(index)` now delegates
  to it.

### Module / navigation

- [x] **MOD-H1 — Stale `screen` after nested display recovery — UAF on tg5050** —
  `module_playlist.c:300` (runWithPlaylist call), `module_library.c:81/84` (PlayerModule_run/PlaylistModule_run)
  Sub-modules consume `DisplayHelper_getReinitScreen()` and update only their local `screen`; callers
  keep rendering/flipping the old freed surface after the sub-module returns (main loop refetches at
  `musicplayer.c:151-153`, proving the contract; these two callers miss it).
  **Fix**: refetch/propagate the current screen after sub-module calls. *(fixed in batch 1)*

- [x] **MOD-H2 — Power management dead while any overlay/dialog is open** —
  `module_common.c` (context menu / controls-help consume-and-return before `PWR_update`); every module
  loop `continue`s on `input_consumed` before `ModuleCommon_PWR_update`; same for AddToPlaylist overlay
  (`module_player.c:622-639`) and confirm dialogs (`module_player.c:642-669`, `module_playlist.c:139-193`).
  Open a menu and set the device down → no autosleep, no power button, battery drain. (Predates the
  context menu — controls-help and confirm dialogs always did this — but the menu widens exposure.)
  **Fix**: run `PWR_update` (or a slimmed power-only tick) in the consumed paths.

### UI / metadata

- [x] **UI-H1 — Podcast artwork downloads run on the render thread; failures retry every frame** —
  Top Shows / Search: `ui_podcast.c:764-769, 849-854` (`artwork_fetch_one` `:222-269`); playing screen:
  `ui_podcast.c:1481-1486` (`podcast_fetch_artwork` `:59-128`, broken dedup guard at `:64` — on failure
  `podcast_artwork` stays NULL so the guard never trips)
  A failed fetch isn't remembered and doesn't break the per-frame loop → repeated 15-30s blocking wget
  calls; the playing-screen case hits normal offline playback (missing `artwork.jpg`) on every redraw.
  **Fix**: worker thread (mirror `album_art.c` pattern) + failed-URL set with backoff.

- [x] **MOD-M1 (promoted to batch 1) — Quit swallowed from playlist playback** — `module_playlist.c:300`
  `PlayerModule_runWithPlaylist()` return discarded; MENU+SELECT-confirmed quit during playlist playback
  returns `MODULE_EXIT_QUIT` (`module_player.c:954-958`) and PlaylistModule drops it.
  **Fix**: propagate the exit reason. *(fixed in batch 1)*

---

## MEDIUM

### Playback core
- [x] **PB-M1** — Cross-thread stream flags unsynchronized (`stream_seeking`/`seek_target_frame`/`stream_running`/
  `stream_eof`/`current_sample_rate`; writers `player.c:2260-2261, 1370-1373, 2187`; lock-free reader
  `player.c:1192-1204`). ARM weak memory → stale/lost seeks; genuine UB. Fix: atomics or the existing mutex.
- [-] **PB-M2** — FLAC Vorbis-comment walk trusts file-supplied lengths — `player.c:1959-1976`.
  `commentLength` (unaligned, untrusted 32-bit) not checked against block size before malloc+memcpy →
  OOB read/crash opening a corrupt file. Use `drflac_next_vorbis_comment()`.
- [x] **PB-M3** — M4A `samplerate_hz == 0` accepted → division by zero — `player.c:2056-2057` (validation
  missing at `player.c:419-422`; AAC path checks at `:544`).
- [x] **PB-M4** — Failed device reopen leaves speaker amp force-muted forever — `player.c:1501` +
  early return `:1530-1532` skips `SetVolume(GetVolume())`. Transient ALSA failure → app silent until restart.

### Radio
- [x] **RAD-M1** — HLS never reaches `RADIO_STATE_PLAYING`: threshold `audio_ring_count > SAMPLE_RATE*2*10`
  equals ring capacity exactly; write caps below it (`radio.c:1028-1030` vs `:186-190`). Auto screen-off
  (`module_radio.c:442`) never fires on HLS → battery drain. Fix with RAD-M7 (~6 lines).
- [x] **RAD-M2** — Live-playlist refresh mutates `radio.hls` without `hls_mutex` + stale prefetch survives
  renumbering (`radio.c:779-807` vs `:691-700`, consume `:872-879`) → garbage URL fetch / out-of-order audio.
- [x] **RAD-M3** — `thread_running` left true when `pthread_create` fails (`radio.c:1595-1606, 1714-1727`) →
  later `pthread_join` on garbage → UB exactly under resource pressure.
  Fixed alongside RAD-H3: the two thread-create sites collapsed into one in `Radio_play`, which now sets
  `thread_running = false` on `pthread_create` failure.
- [x] **RAD-M4** — TS demuxer trusts in-packet lengths past the 188-byte packet (PMT `radio_hls.c:397-425`,
  PAT `:383-390`, PES `:434-438`) → heap OOB reads up to ~4KB on corrupt segments.
- [x] **RAD-M5** — `strstr` on unterminated TXXX frame data (`radio_hls.c:269`) → OOB read past segment buffer.
- [x] **RAD-M6** — `radio_net_resolve_url` plain-HTTP hop: no timeouts (`radio_net.c:768-791, :826`) → UI hang.
  recv/send timeouts added in batch 2; and with RAD-H3's full fix the resolve now runs on the stream
  thread, so its connect() SYN-stall no longer blocks the UI at all (bounded worker-side wait only).
- [x] **RAD-M7** — HLS ring backpressure only between segments with 1s headroom (`radio.c:828-831`; silent
  drop `:186-190`) → audible skips when segments queue up.

### Podcast
- [x] **POD-M1** — `Podcast_subscribeFromItunes` stamps itunes_id/artwork onto `subscriptions[count-1]` even
  when `Podcast_subscribe` returned 0 for "already subscribed" (`podcast.c:845-856` vs `:748-750`) →
  corrupts an unrelated feed's identity.
- [x] **POD-M2** — Cancelling one download aborts the whole run; survivors sit PENDING until app restart
  (`podcast.c:1882-1886`; nothing clears `download_should_stop` and restarts).
- [~] **POD-M3** — UI dereferences the raw queue pointer lock-free (`module_podcast.c:331-349, 746-752,
  851-857`; `ui_podcast.c:1307`) → cancel can target the wrong episode; torn strings. (Same family: UI-M2.)
  Largely resolved by the POD-H1 rework (the thread no longer compacts/rebinds during a live download;
  the active row's progress comes from the stable global). Remaining lock-free iteration of the queue
  array for rendering is benign torn-string territory; a full copy-out accessor (IMP-1) still recommended.
- [ ] **POD-M4** — Episode paths `Podcasts/<feed title>/<episode title>.mp3` are non-unique
  (`podcast.c:1619-1645`): same-title episodes overwrite each other; same-sanitized-title *feeds* share a
  dir that unsubscribe recursively deletes (`podcast.c:929-931`). Fix: `feed_id/guid-hash.ext` (IMP-3).
- [x] **POD-M5** — `current_feed`/`current_feed_index` not adjusted by unsubscribe while audio active
  (`podcast.c:203, 1480-1482`; reachable path `module_podcast.c:945-956, 823-829`) → `Podcast_stop`
  (`podcast.c:1498-1508`) saves progress/continue-listening under the wrong feed.
- [x] **POD-M6** — Refresh thread and main thread race on `episodes.json` with truncate-in-place writes
  (`podcast.c:1050` → parson `fopen("w")`; reads `podcast.c:381`; lock-free RMW `podcast.c:2220-2236`);
  hard power-off mid-write leaves permanently unparseable JSON. Fix: atomic write (IMP-2) + lock.

### Module / navigation
- [x] **MOD-M2** — Background music stalls at track end outside the main menu: only `module_menu.c:50`
  calls `Background_tick()`. Track ends while browsing Files/Playlists/Settings → silence until returning
  to the menu. Fix: tick from `ModuleCommon_PWR_update` (it already pumps `Player_update`).
- [x] **MOD-M3** — Main-menu first row goes stale when background playback ends while on the menu
  (`module_menu.c:56-62` recomputes but never sets dirty on change; `has_first` flips) → A opens the page
  *below* the one shown.
- [x] **MOD-M4** — M3U rewrite truncate-in-place, no tmp+rename, no fsync (`playlist_m3u.c:205-223`); failed
  `strdup` silently drops a line (`:173-175`). Power loss → playlist truncated/empty.
- [x] **MOD-M5** — `Browser_loadDirectory` two-pass count/fill writes with no `idx < count` bound
  (`browser.c:94, :121-150, :162-171`); dir growing between passes (MTP/network copy while browsing) →
  heap overflow. One-line clamp.

### UI / metadata
- [x] **UI-M1** — Lyrics fetch thread publishes `lyrics_lines/line_count/available` with no lock/barrier +
  check-then-write generation TOCTOU (`lyrics.c:190-195, 305-310`; readers `:370-415` via
  `ui_music.c:469-484`) → stale lyrics for the wrong track; torn lines on ARM. Fix: adopt `album_art.c`'s
  mutex+join pattern.
- [~] **UI-M2** — Download queue iterated unlocked in render (`ui_podcast.c:1306-1308, 1342-1345, 451-453`)
  while the thread compacts (`podcast.c:1905-1923`) → torn rows, selection identity swap.
  Mitigated by POD-H1 (compaction now only happens at run-start/end, not mid-download; active progress is
  guid-matched from the stable global). Full copy-out accessor still tracked under IMP-1.

---

## LOW

_LOW pass 2026-08-13: 19 fixed (`[x]`), 8 accepted/deferred (`[-]`). Accepted rationale — **PB-L3**:
"prefer front cover" is cosmetic and a real fix needs a new `album_art_is_front` struct field for
negligible benefit (most MP3s carry one APIC). **RAD-L1**: torn `error_msg` is cosmetic (always
NUL-terminated). **RAD-L4**: curated-station contiguity only breaks with duplicate country codes across
readdir-interleaved files — not the shipped curated data; proper fix = group-by-country at load. **POD-L3**:
Atom `rel`-aware link needs per-link attribute tracking in the SAX parser and risks the common RSS
`<enclosure>` path. **POD-L4**: worker→main flag fences — narrow window on aarch64 (`volatile`); proper fix
folds into the broader atomics hardening. **POD-L5**: `http_download.c` has no live callers (podcast uses
`wget_download_file`) — fix before rewiring. **UI-L2**: raw `Podcast_getSubscription` pointer during refresh
→ resolved by IMP-1 copy-out. **UI-L4**: 32-bit hash cache-filename collisions need a cache-format change
(embed artist/title + verify)._

- [x] **PB-L1** — `pthread_create` result ignored (`player.c:2066`); failure → later `pthread_join` UB.
- [x] **PB-L2** — Spectrum FFT consumes 1024 int16s but only rejects `< 512` (`spectrum.c:217-228`) → stale
  half-buffer on underrun; check should be `SPECTRUM_FFT_SIZE * 2`.
- [-] **PB-L3** — APIC "prefer front cover" dead code (`player.c:1819` gate vs `:1856-1866`).
- [x] **PB-L4** — ID3v1 clobbers valid ID3v2 title containing "." (`player.c:1683, :1885`).
- [x] **PB-L5** — Init-failure cleanup writes all-zero settings (`musicplayer.c:76-79` + `:166`), resetting
  valid zero-valued preferences.
- [x] **PB-L6** — Signal-handler `quit` is plain `static bool` (`musicplayer.c:34-46`); should be
  `volatile sig_atomic_t`.
- [x] **PB-L7** — Mid-track speed change rescales *all* elapsed time (`player.c:1363-1364`): 1.0×→2.0× at
  3:00 displays 6:00. Accumulate per-callback instead.
- [-] **RAD-L1** — `radio.error_msg` torn read (write `radio.c:1108-1113`, read `:1841-1843` →
  `ui_radio.c:317`). Cosmetic.
- [x] **RAD-L2** — HLS master-playlist recursion unbounded (`radio_hls.c:168-180`); hostile playlist →
  hang/stack exhaustion. Add depth cap.
- [x] **RAD-L3** — `radio_hls_resolve_url` leaves `result` unwritten on one path (`radio_hls.c:42-56`);
  latent (base always has scheme today).
- [-] **RAD-L4** — Curated stations assume one contiguous run per country code (`radio_curated.c:152-166`);
  duplicate-code JSON files interleaved by readdir order list wrong stations (in-bounds).
- [x] **RAD-L5** — `parse_headers` accepts truncated 4KB header block (`radio.c:512-526`; radio_net was
  already raised to 8KB for this) → header bytes consumed as audio at startup.
- [x] **POD-L1** — RSS date parse uses uninitialized hour/min/sec when sscanf matches fewer fields
  (`podcast_rss.c:58-77, 80-89`).
- [x] **POD-L2** — XML element-stack desync past depth 32 (`podcast_rss.c:126-138`); misclassifies content
  after a deep subtree (memory-safe).
- [-] **POD-L3** — First `<link href>` wins as episode URL; Atom `rel="alternate"` webpage links beat
  enclosures (`podcast_rss.c:349-356`).
- [-] **POD-L4** — Worker→main handshakes (search/top-shows/refresh_completed) use plain flags, no fences
  (`podcast.c:1224-1239, 710-720`); narrow ARM race.
- [-] **POD-L5** — `http_download.c`: success on 404 bodies/cancel/truncation (`:473-477, 436-438`,
  Content-Length parsed `:315-321` but never compared); uncapped WANT_READ retry loops, CRLF loop lacks
  `should_stop` (`:234-239, 416-431`); unchecked `fwrite` (`:409, 456`). **Latent — no live callers**
  (podcast uses `wget_download_file`); kept as the documented fallback, fix before rewiring.
- [x] **POD-L6** — Queue dedup matches `episode_guid` only, ignoring `feed_url` (`podcast.c:1725`); guid
  falls back to enclosure URL truncated to 127 chars (`podcast_rss.c:292`) → cross-feed evictions possible.
- [x] **MOD-L1** — `runWithPlaylist` sets `playlist.track_count = track_count` past the copy cap and doesn't
  validate `start_index` (`module_player.c:906-910`); latent until a caller passes >500.
- [x] **MOD-L2** — Delete restores selection to row 0: `UI_listViewReset` inside `load_directory` zeroes
  `selected`, making the follow-up clamp dead code (`module_player.c:648-652`). Rename gets it right.
- [x] **MOD-L3** — Unterminated `FileEntry` strings on malloc'd entries for 255-char names / ≥511-char paths
  (`browser.c:144-145`); related silent truncations `playlist.c:89-90, 184-185`, `module_player.c:351-353`.
- [x] **MOD-L4** — Playlist names unsanitized: `/` fails `fopen` and every failure toasts "Already exists"
  (`playlist_m3u.c:84-93, 110-111`; `module_playlist.c:84, 118-119`); silent failure in
  `add_to_playlist.c:127-133`. Contrast `rename_browser_entry` which rejects `/`.
- [x] **MOD-L5** — `remove_recursive` unbounded depth, ~1KB stack/frame (`module_player.c:296-324`); scan
  paths cap at 3/10, delete is the outlier.
- [x] **UI-L1** — Thumbnail cache key truncated to 16 chars but compared full (`ui_podcast.c:144, 172-182`
  vs `itunes_id[32]`); latent per-frame decode loop one size-mismatch away.
- [-] **UI-L2** — `Podcast_getSubscription()` raw pointer read while refresh thread rewrites the struct
  (`ui_podcast.c:873, 1470`) → torn strings; torn `artwork_url` can get fetched+cached as artwork.jpg.
- [x] **UI-L3** — Future `pub_date` renders "-3 days ago" (`ui_podcast.c:372-379`).
- [-] **UI-L4** — Art/lyrics cache filenames are bare 32-bit hashes of artist+title (`album_art.c:65-75`,
  `lyrics.c:41-46`); collisions silently serve the wrong art/lyrics forever.

---

## IMPROVEMENTS

_Improvements pass 2026-08-13: 17 done (`[x]`), 3 not done (`[-]`). Also fixed two HIGHs that had slipped
through the batch plan and were still open: **UI-H1** (podcast artwork fetched with a blocking wget on the
render thread — moved to a background worker with a failed-URL backoff set; = IMP-6) and **MOD-H2** (power
management skipped while a context menu / controls-help overlay was open — a `pump_power` now runs in those
paths; consolidating it also fixed the double PWR_update, IMP-12). Not done: **IMP-3** (skipped per request
— guid-based episode filenames + download migration). **IMP-5** (shared mbedTLS connect helper) — a
maintainability refactor across radio.c/radio_net.c; the functional timeout drift that motivated it is
already fixed (batch 2 + RAD-H3), so the refactor risk isn't worth it now. **IMP-20** (album-art bg cache
identity) — already mitigated: `cleanup_album_art_background()` fully invalidates the cache and every art
provider calls it on swap, so the pointer-alias window is closed at the source; only the centralization
nicety remains._

- [x] **IMP-1** — Copy-out accessors for all cross-thread state: download queue (mirror `Podcast_getEpisode`
  contract), subscriptions, lyrics. Structurally fixes POD-H1/M3, UI-M2/L2, UI-M1.
- [x] **IMP-2** — Atomic persistence everywhere: `path.tmp` + `rename()` (+fsync) for subscriptions,
  episodes, progress, queue JSON and M3U rewrites. Fixes POD-M6, MOD-M4 class.
- [-] **IMP-3** — Stable episode filenames `feed_id/guid-hash.ext` (extension from MIME/URL, not `.mp3`).
  Fixes POD-M4 and the unsubscribe cross-delete.
- [x] **IMP-4** — Move radio connect phase (resolve/redirect/playlist/TLS) into the stream thread with
  CONNECTING as the UI contract — kills RAD-H3/M6/L2 class and makes station switching responsive.
- [-] **IMP-5** — One shared mbedTLS connect/teardown helper (radio.c / radio_net.c fetch / resolve carry
  three drifted ~80-line copies; the drift *is* RAD-H3/M6).
- [x] **IMP-6** — Podcast artwork on a worker thread (album_art.c pattern) + failed-URL backoff set (UI-H1);
  same pattern for lyrics (UI-M1).
- [x] **IMP-7** — `settings.c:178-180`: `system("mkdir -p")` → `mkdir(2)` (forks a shell per settings press).
- [x] **IMP-8** — Decode hot path: hoist per-chunk `malloc/free` (mono/float buffers, `player.c:592/609/627`,
  `resample_chunk` `player.c:1086-1087`) into one-time allocations.
- [x] **IMP-9** — Cap `parse_id3v2` `malloc(tag_size)` (`player.c:1725-1729`) — syncsafe header allows 256MB
  from an untrusted file.
- [x] **IMP-10** — Persistent spectrum surface + precomputed gradient (`spectrum.c:352` allocs/frees a full
  ARGB surface per frame, 1-px FillRect rows).
- [x] **IMP-11** — `ModuleCommon_ctxAdd` bounds against `CONTEXTMENU_MAX_ITEMS` (16) but callers pass 4-slot
  stack arrays (`module_common.c:250`, `module_player.c:677`) — pass real capacity.
- [x] **IMP-12** — `PWR_update` runs twice per frame (tail of `ModuleCommon_handleGlobalInput` + every
  module's `ModuleCommon_PWR_update`) — consolidate.
- [x] **IMP-13** — Folder add is O(n·m) file I/O (`add_to_playlist.c:72-83` re-reads the .m3u per track for
  dup checks) — load paths once, append in one open.
- [x] **IMP-14** — Aliased `strncpy` (restrict UB) in ".." navigation: `module_player.c:405-410` truncates
  `current_path` in place then passes it as both src and dst to `Browser_loadDirectory` (`browser.c:43`);
  copy first like the ACTIVATED path does.
- [x] **IMP-15** — 50-playlist cap silently hides playlists (`playlist_m3u.c:44`); surface or raise.
- [x] **IMP-16** — Progress table (500-entry cap, `podcast.c:208`) stops recording when full — evict oldest /
  prune unsubscribed feeds.
- [x] **IMP-17** — Verify downloads: compare final size against captured Content-Length + status line in
  `wget_download_file` before reporting success.
- [x] **IMP-18** — Detect `#EXT-X-KEY` (encrypted HLS) and error clearly instead of decoding noise.
- [x] **IMP-19** — HLS: check ring space inside the decode loop + lower the PLAYING threshold to a reachable
  value (fixes RAD-M1 + RAD-M7 together).
- [-] **IMP-20** — Album-art background cache identity is pointer+dims (`ui_album_art.c:26-31`) and can alias
  a recycled malloc block; invalidate from the art providers instead of 8 scattered cleanup calls.

---

## Verified-clean areas (checked, recorded so future audits don't re-litigate)

- Every `TTF_Render*`/`CreateRGBSurface` in the 9 UI files freed on all paths (incl. partial-failure paths
  `ui_album_art.c:94-98`, `ui_music.c:421-444`).
- `wget_fetch` / `radio_net_fetch` cap reads at `size-1` → all `buf[bytes]='\0'` writes safe.
- Progress-bar division guarded everywhere (`ui_music.c:140`, `ui_podcast.c:1833`).
- Hand-rolled podcast list scroll math clamps against live counts; `Podcast_getEpisode` bounds-checks and
  copies out under mutex (`podcast.c:451-499`).
- `album_art.c` mutex+join design sound (unlocked `result_ready` synchronized by join).
- `sanitize_for_filename` neutralizes traversal incl. `.`/`..`/empty (`podcast.c:1597-1616`).
- Audio callback trylock vs `Player_stop` teardown ordering correct (join → lock → free); ID3v2 frame-size
  wrap check `player.c:1764-1769` correct; circular buffer index math sound.
- ICY `meta_len` ≤ 4080 by construction; `sorted_station_indices[256]` correctly capped; radio album-art
  surface freed only on main thread.
- Context-menu dispatch in `PlayerModule_run` holding `entry` across `rename_browser_entry` is safe as
  written (`is_dir` read before reload; DELETE/ADD copy before reload) — fragile, see MOD-L2/L3 notes.
- `icon-ops.png` exists in skeleton/SYSTEM/res (not a typo).
