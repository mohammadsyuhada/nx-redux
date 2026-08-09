# Code Audit — `workspace/all/` — remaining work (updated 2026-08-09)

Static review of first-party C/C++ in `workspace/all/` (vendored trees excluded). This file now tracks only what is **still open** — completed fixes have been removed. See git history / the commit for the applied changes.

## Completed (build-verified tg5040, all touched modules green)

All 6 High findings, plus a batch of Mediums/Lows and one dedup, are fixed and compiling:

- **Highs:** sync.c peer-IP validation; podcast `..` traversal guard; ID3v2.3 overflow; gbalink heartbeat deadlock; PAD_setAnalog repeat-mask; updater integrity check.
- **H2 (updater) — hash-only, accepted design:** verifies the downloaded zip's SHA-256 against GitHub's asset `digest` before extracting, aborts on mismatch, skips if no digest present. Stops corrupted-download bricking. **Not MITM-proof** (digest rides the same TLS-disabled channel); signing declined, TLS can't be re-enabled (no CA bundle). Accepted residual risk for this device.
- **Mediums:** common memory-safety (getEmuName, folderPath, GFX_blitMessage/blitText, GFX_truncateText, PLAT_updateShader); minarch Menu_loadState + minarch_reloadGame; netplay recv_packet drain; ffplay filtergraph escaping; osdctl per-platform struct; taskset CPU_SET bound; portmaster shell-free copy; settings wifi/bt toggle + bt-action use-after-return; musicplayer album_art race.
- **Mediums batch 2 (2026-08-09, both platforms compile clean; Brick smoke-tested):**
  - *minarch core-options reset* — new `Config_reapplyOptions()` (`ma_config.c`): if `Config_free` already ran (`config.released`), transiently `Config_load` → `Config_readOptions` → `Config_free`; wired into all 5 SET_CORE_OPTIONS/SET_VARIABLES handlers in `ma_environment.c` (V2 handlers previously never re-read at all).
  - *settings scanner race* — `settings_menu.c` A-press now acts on a `SettingItem` snapshot taken before the rdlock release; `wifi_network_press`/`bt_device_press` copy the net/dev info out under the page lock before building the options submenu. Verified on Brick: WiFi toggle → scan list → network options → Connect, all good.
  - *musicplayer radio-metadata race* — `radio.c` gained `meta_mutex`; all stream-thread writers (ICY headers, ICY metadata, HLS EXTINF/ID3/bitrate, play-reset) and `Radio_getMetadata()` (now returns a locked snapshot) go through it.
  - *musicplayer `Podcast_getEpisode`* — API changed to copy-out: `bool Podcast_getEpisode(int, int, PodcastEpisode* out)` + `Podcast_setEpisodeProgress()` for cache write-back; all ~20 call sites across podcast.c / module_podcast.c / ui_podcast.c converted, no pointers into `episode_cache` escape anymore.
- **Cleanup:** all verified unused includes removed; `url_encode` ×3 → `common/utils.c urlEncode()`.

---

## MEDIUM — still open

- **scraper `-k` / http.c `-k` / wget `--no-check-certificate`** — `scraper/scraper_api.c:405-408`, `common/http.c:145-173`, `common/wget_fetch.c:49,123`. TLS validation disabled → MITM can substitute downloads. **Accepted (same class as H2): removing breaks all HTTPS, no CA bundle ships on device.**

---

## LOW — open backlog

- **`common/utils.c:63-69` `truncateString`** `&string[max_len-4]` underflows for `max_len<4` (callers pass sane sizes).
- **`common/utils.c:534-559` `getFile`/`allocFile`** don't guard `ftell==-1`; unseekable file → `contents[SIZE_MAX]='\0'`. `getFile` also underflows `buffer_size-1` when `buffer_size==0`.
- **`common/audio_manager.c:103,116`** `strncpy(name,line+8,sizeof(name)-1)` may leave the buffer unterminated for ≥255-char `/proc/bus/input/devices` lines → `strstr` over-read.
- **`common/generic_bt.c:547,667`** 1-byte over-read of `name_start = line+7+18` on a `bluetoothctl` line with no trailing space (stays inside the 8 KB buffer).
- **`common/generic_video.c:227-237` `link_program`** unchecked `ftell`/`malloc(length)`/`fread` on a shader-cache binary; corrupt cache can crash (recoverable path).
- **`common/http.c` POST content-type** interpolated raw inside `'…'` (currently only ever NULL from callers); `escaped_ct` is computed, freed, never used (dead + latent injection).
- **`common/config.c:121 vs 1153-1158`** `CFG_init` reads from the compile-time macro path but `CFG_sync` writes to `$SHARED_USERDATA_PATH`; if the env var is unset, every `CFG_sync` silently no-ops → settings changes discarded.
- **minarch (8):** `ma_config.c:1360-1362` `sprintf(getenv("DEVICE"))` into `char[64]`; `ma_config.c:1275,1303` `sprintf("bind %s", core-descriptor)` into `char[256]` (core-supplied, uncapped); `ma_menu.c:1184-1191,1247-1255,1144` + TTF renders — NULL-deref when `GFX_GL_screenCapture`/`TTF_Render*` fail (OOM); `chd_reader.c:453-457` audio-track `data_size` not clamped to `frame_size` → heap over-read on a hostile `.chd`; `ma_video.c:536-555` `SCALE_ASPECT_SCREEN` computes `scale=0` for oversized sources → 0-size dst + per-frame re-init (black screen/stutter); `ma_rewind.c:478-481` benign unlocked read of `generation` (TSan only); one-shot init allocs never freed (exit reclaims).
- **libgametimedb (5):** `gametimedb.c:66-75` `free_play_activities` leaks `rom->{type,name,file_path,image_path}`; `:212-220` `__ensure_rel_path` leaks strdup'd + `replaceString2` buffers (per lookup); `:199-201` `last_played_at` guard checks column 9 of a 9-column query → always NULL (dead field); `:178-191` `ROM` not zero-initialized → garbage `image_path` if `file_path` NULL; `:341` `_get_active_rom_path` `strncpy` may leave output unterminated.
- **`show2/show2.cpp:209,283,305,309,612-633`** `std::stoi/std::stoul` throw uncaught on non-numeric input → `std::terminate`; FIFO daemon path (`PROGRESS:abc`) makes it externally reachable. `parseColor` indexes `hex[0]` on a possibly-empty string.
- **nextui:** `types.c:32-40` `Array_unshift` assumes `Array_push` succeeded (OOB write on OOM realloc-fail); `launcher.c:189-221` `char cmd[MAX_PATH*2]` truncates near-`MAX_PATH` paths and `escapeSingleQuotes` bail-on-overflow can yield unbalanced quotes.
- **settings/extras:** `settings_bootlogo.c:112-116` bundled `.bmp` name unescaped in `cp '%s'`; `extras/extras.c:714-722` catalog `<id>` dir name unescaped in `popen` (bundled today); `settings_input.c` / `settings_clock.c` TTF renders unchecked for NULL.
- **`netplay/gbalink.c:1355-1360`** oversized-packet path resets the whole stream buffer, discarding already-buffered valid packets. **`netplay/netplay.c:1038-1085`** `np.tcp_fd` read/`select`/`recv` without `np.mutex` (latent TOCTOU vs close/reassign).
- **`mediaplayer/ffplay_engine.c:18,238`** `ffplay_pid` accessed across SIGTERM handler with no coordination → ffplay can be orphaned on quit (uncertain).

---

## Duplication → extract to shared helpers (open)

Ranked by value (LOC × copies × low risk).

1. **Boxart `.media` path resolution — 6 copies / 4 modules** (`nextui/gamelist.c:224-238` + `launcher.c:115-140` + `search.c:175`, `ratools/ratools_browser.c:51-80`, `libgametimedb/gametimedb.c:78-93`, `scraper/scraper.c:522`). One copy references another by comment — drift risk is live. → `ROM_mediaArtPath()` + `ROM_findArt()` in `common/utils.c`. ~60-80 LOC. *Moderate.*
2. **M3U handling nextui↔minarch** — folder-named `.m3u` derivation (`minarch/ma_game.c:111-135` ≡ `nextui/content.c:244-267`) and disc-line iteration (`nextui/content.c:820-850` ≡ `minarch/ma_menu.c:95-126`). → `findFolderM3u()` + callback `M3U_forEachDisc()` in common. ~55 LOC. *Moderate.*
3. **`settings/settings.c:131-150 exec_command` ≡ `common/generic_wifi.c:41-68 wifi_run_cmd`** (third run_cmd-capture variant). → `run_cmd_capture()` returning exit code. ~25 LOC. *Moderate (settings ignores exit code today).*
4. **`musicplayer/podcast.c:308-326 mkdir_recursive` ≡ `common/utils.c:479-491 mkdir_p`** — musicplayer already links utils. ~18 LOC. *Trivial.*
5. **Blocking modal loop ×5 in nextui** (`gamelist.c` confirmModal/settingsPinAllows/pickCollectionModal/artFetchNotice/artFetchModal; also `settings.c`, `extras.c` dialogs). → `runModalLoop(render_cb, handle_cb, ctx)`. *Moderate.*
6. **WiFi/BT scanner-page machinery duplicated** (`settings/settings_wifi.c` ≡ `settings_bt.c`: toggle threads, pooled option submenus, custom row draw, scanner rebuild). Natural home to also fix the settings scanner race above.
7. **DJB2 hash ×3** (`musicplayer/{lyrics.c:34,album_art.c:42}`, `nextui/types.c:82`) + `get_cache_filepath` twice → `hashString()` in utils. ~20 LOC.
8. **Image-completeness check** (`musicplayer/podcast.c:525 is_image_data_complete` ≡ `ui_podcast.c is_image_complete`) + circular/rounded thumbnail load (`ui_podcast.c:207/396`) overlaps `common/ui/ui_image.c UI_loadRoundedImage`. → `UI_roundedFromSurface()`. ~60 LOC. *Moderate.*
9. **`format_duration`** (`musicplayer/ui_podcast.c:514-544`) ≡ `common/utils.c:265-274 format_time` (+ dashes guard). ~15 LOC. *Trivial.*
10. **`nextui/imgloader.c:265-275`** hand-rolls `UI_calcImageFit` while calling the real one at :440. ~8 LOC. *Trivial.*
11. **minarch internal:** `Config_restore` (`ma_config.c:1469-1483`) rebuilds paths byte-identical to `Config_getPath` (drift risk); screenshot capture→convert→spawn block ×2-4 (`ma_menu.c`, `minarch.c:481`); `Config_syncShaders` SH_SHADER1/2/3 triplets (`ma_shaders.c:112-183`) → `(pass,kind)` table.
12. **netplay internal:** link-engine boilerplate triplicated (`netplay.c`/`gbalink.c`/`gblink.c` restartBroadcast/stopHost/connect/listen loops); gbalink disconnect state-machine block ×3 (`recv_packet:1286-1332`, `pollReceive:999-1035`) → `gbalink_handle_remote_gone()`.

**Deferred (batch with tracked item B3):** `mediaplayer/iptv_curated.c` ≈ `musicplayer/radio_curated.c` (~55% identical curated-country loader).

---

## Dead code (open — safe removals)

- **`mediaplayer/settings.c`** — empty stub (empty struct, all no-op functions), a clone leftover from the musicplayer split. Removable.
- **`common/http.c` `escaped_ct`** — computed and freed, never used.
- **`libgametimedb` `last_played_at`** — always NULL as written, unused by UI.
