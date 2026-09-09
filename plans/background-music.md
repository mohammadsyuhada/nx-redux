# Background Music Player with quick-menu controls

## Status, confirmed behavior, and scope

Planning handoff only; no firmware implementation, build, or deployment.
A live-device inspection and short silent ALSA probes were completed; their
limited evidence is recorded below. The confirmed product behavior is:

- Music continues while the Music Player UI is closed, while browsing NX Redux,
  and during gameplay.
- Gameplay audio and music are concurrent streams. The design must not suspend,
  stop, mute, duck, or release the music stream when an emulator or game starts.
- The existing Music Player remains the feature target, not the fullscreen video
  Media Player. Local music, radio, downloaded podcasts, playlists, queue,
  repeat/shuffle, resume, artwork, lyrics, visualization, settings, and HID
  media controls remain in scope and must not be lost when playback outlives its
  UI.
- The OSD exposes only previous, play/pause, next, and current title/artist/
  state. It does not add a seek control, volume control, ducking, or another
  mixer policy. Existing hardware volume remains global.

Video audio policy is deliberately separate and unsettled by this clarification.
Do not infer that fullscreen video must mix, stop, or take ownership of music;
inspect and specify that product behavior in a separate decision. That open
video question must not block the confirmed browse/gameplay design.

The proposed music daemon, once introduced, is the sole owner of Music Player
playback (local/radio/podcast decode, queue, and music HID commands). It is not
the owner or mixer for all system audio: emulators, PortMaster and other
standalone tools retain their own audio paths. Do not claim that the daemon or
ALSA `dmix` has mixed streams until the hardware matrix proves it.

## Evidence and source constraints

The following source facts constrain implementation. Symbols are preferred over
line numbers because they move.

| Seam | Source and symbols | Constraint |
| --- | --- | --- |
| Output routing/rates | `.dev/AUDIO.md`; `workspace/all/common/audio_manager.c:AudioMgr_pickRate`, `AudioMgr_pollEvents`; `workspace/all/audiomon` | `default` is routed through plug/softvol/plug to a dmix slave locked at 48 kHz. `audiomon` publishes `/tmp/nx_audio_sink` and applies BT rate limits. This describes the speaker route. `audiomon.c:write_audio_file` overrides default with `plug -> hw:<card>,0` for USB and `plug -> bluealsa` for BT, neither containing dmix; external-output sharing requires an explicit design and validation. |
| Minarch output | `workspace/all/common/api.c:SND_init`, `SND_resetAudio`; `workspace/all/minarch/ma_audio.c`; `workspace/all/minarch/minarch.c` | `SND_init` calls `AudioMgr_pickRate`, opens SDL with `SDL_AUDIO_ALLOW_ANY_CHANGE`, and records the obtained rate for buffering. `Audio_checkAndResetIfNeeded` resets on sink events. `AudioMgr_pollEvents` is called in the main loop. Any shared-output change must preserve these seams. |
| Music output | `workspace/all/musicplayer/player.c:get_target_sample_rate`, `Player_init`, `reconfigure_audio_device`, `reopen_audio_device`, `Player_update`, `Player_quit`; `workspace/all/musicplayer/radio.c:radio_configure_rate` | Music opens its own SDL device, already picks a sink-compatible rate, resamples radio to device rate, and reopens on sink callback. Reopen must not globally mute or disrupt another process. |
| Global controls | `workspace/all/musicplayer/musicplayer.c` (`SetRawVolume(0)`, `Player_init`, `SetVolume(GetVolume())`); `workspace/tg5040/libmsettings/msettings.c`; `workspace/tg5050/libmsettings/msettings.c`; `workspace/all/audiomon/audiomon.c` | Mute/volume and mixer commands affect shared output, not only music. Compare both platform copies before changing any mute/amp behavior. A per-open global mute/restore sequence cannot be assumed safe during gameplay. |
| Existing emulator fixes | `workspace/all/other/mupen64plus/mupen64plus-audio-sdl.patch`; `workspace/all/other/flycast/flycast.patch`; `workspace/all/other/sdl2-drastic/0006-add-hook-for-drastic.patch` | N64 has 48 kHz, drift, buffering, and underrun handling. Flycast uses `NX_AUDIO_RATE`, SDL conversion, and a native-rate fallback. Drastic exposes private `nds_audio_pause`, `nds_audio_revert_pause_state`, and `nds_audio_exit` hooks. These are integration points, not proof of concurrency. |
| Standalone launch | `workspace/all/portmaster/portmaster.c`; platform pak launch scripts and emulator paks | PortMaster launches a bundled external runtime and its game scripts; its actual game audio implementation is not established by this source. Do not issue a generic music suspend command or assume it uses `AudioMgr_pickRate`. |
| UI/owner lifetime | `workspace/all/musicplayer/musicplayer.c`; `module_player.c`, `background.c`, `playlist.c/h`, `radio.c`, `podcast.c`, `resume.c/h`, `ui_*` | Current UI owns `Player_init`/`Player_quit` and source ticks. A future owner must preserve source-specific behavior and provide copied, synchronized snapshots rather than SDL pointers. |
| Launcher/power | `skeleton/SYSTEM/tg5040/paks/MinUI.pak/launch.sh`; `skeleton/SYSTEM/tg5050/paks/MinUI.pak/launch.sh`; `workspace/all/nextui/launcher.c`; `workspace/all/common/api.c:PWR_deepSleep`, sleep/wake paths; OSD `toggle_power/set.sh` | The launch loop runs foreground apps sequentially. Only sleep/wake and shutdown/poweroff are intentional music lifecycle boundaries. No foreground hold protocol is needed or allowed for gameplay continuity. |
| OSD | `skeleton/SYSTEM/osd/common/widgets/app_music/{config.json,launch.sh}`; four device `osdlayout.json` files; `.dev/OSD.md` | OSD is a separate overlay plane. Existing app-music files are a stub and the closed daemon protocol must be observed before wiring controls. |

## Architecture and contracts

### Music owner and concurrent output

Use one supervisor-owned `musicplayerd.elf` (proposed) as the Music Player
audio owner. The current Music Player UI becomes a client and can detach without
stopping playback. A small `musicplayerctl.elf` (proposed) may provide bounded
local commands for lifecycle and OSD use. Neither process owns emulator audio,
changes another process's SDL device, or presents itself as a system mixer.

The owner starts idle after `audiomon` is available; it does not autoplay at
boot. Its normal states are idle, loaded/paused, playing, and error. A user
pause/stop is an explicit transport decision. There is no foreground/game hold,
foreground suspension command, release-device requirement, resume-on-return
policy, or launch-loop music stop. Starting or leaving a game, browsing, opening
or closing the Music Player UI, and entering/exiting the OSD leave music playing.

Use one bounded local Unix stream socket (proposed
`/tmp/trimui_music/control.sock`) with length-prefixed, fixed-width wire fields
and request/response timeouts. Commands cover load/select, play/pause/stop,
next/previous, seek, repeat/shuffle, snapshot, and shutdown. Do not include a
foreground or gameplay suspension command. Sleep entry/exit is a power-lifecycle
operation, not a gameplay hold; it may stop decode and close the music device
only for the sleep interval. Shutdown is synchronous and bounded. Repeated
shutdown requests and owner cleanup are idempotent; a client disconnect does
not stop music.

Snapshots contain copied source identity, metadata, transport state,
position/duration, source/output rate, queue state, capabilities, and errors.
No raw pointers or SDL surfaces cross the process boundary. Synchronize mutable
state with the same locks/atomics used by writers; IPC serialization by itself
is not a snapshot-consistency guarantee.

### Shared rate and output policy

Before refactoring ownership, establish a shared active-output contract:

1. Read the sink and rates published by `audiomon` and establish one authoritative
   active mixer rate consumed by all streams. `AudioMgr_pickRate(desired)` alone
   is not coordination: different desired rates can return different valid rates.
   Specify publication/consumption of the active rate in audiomon, common audio
   helpers and standalone launch paths before implementing it. While music and
   gameplay are both active, neither source may independently chase its source
   rate and force a device reopen. Each decoder/core resamples in its existing
   high-quality path to the selected stream rate. Do not rely on
   `SDL_AUDIO_ALLOW_ANY_CHANGE`: the ALSA plug layer can accept arbitrary input
   rates without revealing the dmix slave rate.
2. Record requested and obtained SDL rates for every stream, the selected
   `/tmp/nx_audio_sink` state, and active ALSA `hw_params`. A source-rate or
   sink-rate change may require a coordinated reopen and an audible gap; do not
   call it transparent. Prefer keeping the published rate fixed until a sink
   change is complete.
3. Reuse the speaker dmix path first. `audiomon.c:write_audio_file` currently
   routes USB directly through plug to hardware and BT through plug to BlueALSA;
   there is no shared mixer in either generated external-output route. Investigate
   an ALSA shared hardware mixer route for USB using supported device parameters.
   Do not assume dmix can wrap the BlueALSA plugin: establish a compatible shared
   output solution from current upstream documentation and device tests. If a
   single output owner/PCM handoff is needed, specify its integration with every
   emulator before selecting it; do not silently add a general audio server or
   call an exclusive output supported. Speaker-only success is intermediate
   evidence, not completion of supported-output gameplay playback.
4. Resolve the existing Follow source setting explicitly: shared mixing needs a
   stable mixer rate and is not bit-exact per-source playback. Preserve the saved
   preference; publish/display the actual effective source/output rates and any
   shared-mode constraint. Define when source-following is available without
   reopening or disrupting another stream. Do not silently relabel resampled
   shared output as native-rate playback.
5. Keep per-process sink callbacks independent. `AudioMgr_pollEvents` invokes
   one process's callback on its main thread; it does not arbitrate or mix
   other processes. Music sink reopen must close only Music's device. Minarch
   keeps `SND_resetAudio`; N64/Flycast/Drastic/PortMaster behavior is audited at
   its own boundary.

### Global mute, volume, and routing

`SetRawVolume`, `SetMute`, `SetVolume`, and audiomon mixer operations are global.
Music attenuation must remain software-local. Remove the assumption that every
`Player_init` may execute `SetRawVolume(0)` and restore global volume: the
existing sequence in `workspace/all/musicplayer/musicplayer.c` must be made
conditional on a proven output-idle/amp-pop case, or replaced after both
platforms' `msettings.c` copies are compared. It must never globally mute an
active gameplay stream or restore a stale value over a user's change. The same
rule applies to `player.c:reopen_audio_device` and sink hotplug paths. Do not
preserve a global mute blindly merely because it was present before opening.
Do not add ducking or a second music-volume UI control.

### Browsing, sources, and existing features

Move queue/EOF/track policy from `module_player.c`, `background.c`, and
`playlist.c/h` behind the owner while retaining the existing queue and
repeat/shuffle semantics. Keep radio stream configuration in `radio.c` and
`module_radio.c`; keep podcast identity, seek, stop, and progress persistence
in `podcast.c` and `module_podcast.c`. Preserve album-art, lyrics,
visualization, settings, local formats, network reconnect, and resume behavior
with explicit copied/file contracts. HID polling belongs to the one long-lived
Music owner via `AudioMgr_pollHID`; UI and service must not poll the same event
nodes.

## Serial implementation milestones

Each milestone is a bounded diff with focused review before the next. Do not
begin owner/UI migration until Milestone 1 establishes the shared-output gate.

### 0 — Observe the OSD contract without changing firmware

Inspect `app_music/config.json`, `app_music/launch.sh`, `.dev/OSD.md`, and all
four `osdlayout.json` files. On an available device, use temporary artifacts
outside the repository to observe the closed daemon's widget command endpoint,
button events, launch/relaunch behavior, canvas stride/pixel order, update and
teardown semantics, and required icons. Do not invent FIFO semantics, patch the
closed daemon, or route foreground PAD input to the widget.

Gate: record device/firmware, observed commands, and one temporary
input/render proof. Treat one device as insufficient to prove all model-specific
OSD binaries equivalent. This milestone does not decide video audio policy.

### 1 — Prove concurrent ALSA/SDL output and set measured budgets

This is the mandatory bounded serial milestone for shared output correctness.
Build a disposable diagnostic path or logging changes only as needed to capture,
for each process, requested/obtained rate, SDL device status, PCM path, open
file descriptors, and callback timing. Do not add a general mixer service.

Use a temporary playback-only harness reusing the existing engine for this
pre-migration probe; do not require the not-yet-built daemon, UI detach, or OSD
bridge to pass it. Run simultaneous music plus gameplay on **tg5040 and tg5050** for speaker, each
available USB DAC, and a Bluetooth sink only after BlueZ `MediaTransport1` is
actually active. Cover:

- local track, radio, and downloaded podcast with differing source rates;
- minarch, N64, Flycast, Drastic, and representative PortMaster gameplay;
- sink hotplug while both streams play, pause, seek, EOF/queue advance, and
  repeated launch/return cycles;
- record UI detach/reopen and OSD controls as later milestone regression checks,
  not prerequisites for this initial audio probe;
- global volume/mute changes, including the existing amp-pop/open paths, without
  silencing the other stream;
- inspect existing sleep/wake and poweroff effects; validate the completed
  service lifecycle after Milestone 5 rather than requiring it in this probe.

For every run capture `/proc/asound/card*/pcm*p/sub*/hw_params`, SDL obtained
rates, `/proc/<pid>/fd` PCM links, audiomon logs, callback latency/deadline
misses, underrun/drop counters, process CPU, scheduler behavior, and audible
continuity. Measure a quiet single-stream baseline and a concurrent baseline on
each platform/output. Set the implementation's CPU and callback budgets from
those measured p95/p99 timings and available headroom; do not guess a fixed
budget. Acceptance is no new stream starvation or callback deadline misses over
repeated long runs, with any residual underruns/drops and CPU headroom recorded
rather than hidden. A dmix configuration is not a pass by itself.

Gate: ordinary speaker concurrency is evidenced or rejected explicitly; every
USB/BT result is recorded separately; the active-rate authority, external-output
sharing design and all process integration points are written down. Split any
required routing changes into bounded source-specific implementation steps
before owner migration. Do not downgrade the requirement to speaker-only or
pause music on unsupported concurrent paths. Missing hardware remains an
explicit validation gap.

### 2 — Separate Music engine initialization from device lifetime

Files: `workspace/all/musicplayer/player.c/h`, `radio.c/h`, startup in
`musicplayer.c`, and focused tests under `workspace/all/musicplayer/tests/`.
Introduce explicit core-init, device-open, sleep-close, reopen, and final-quit
boundaries. Preserve decoder stop/join ordering, mutex lifetime, buffer/SRC
reset, radio reconnect, source-rate behavior, and copied snapshots. Suppress
only sleep/shutdown reopen; never suppress because a foreground app or game is
running. Audit callback/close/mutex ordering for deadlocks and keep real-time
callbacks allocation-free.

The open path must use the Milestone 1 fixed active-output rate and must not
perform a blind global mute/restore. Verify that a failed reopen cannot leave
hardware globally muted. Keep the current UI working during extraction.

Gate: both platform component builds; local/radio/podcast play/pause/seek/EOF;
repeated sleep-close/wake-reopen and sink changes; speaker/USB/BT tests; and
proof that the device behavior is observed rather than inferred from a paused
SDL handle.

### 3 — Add the owner, client, and packaging

Proposed files include `musicplayerd.c`, `music_service.c/h`,
`music_service_protocol.h`, `music_service_client.c/h`, and `musicplayerctl.c`.
Update the Music Player, `workspace/Makefile`, and root packaging only when the
runtime dependencies are known. Build the daemon idle with no PCM device, move
local queue/EOF policy without duplicating decoder code, and retain album-art/
radio linkage required by `Player`.

Use real local socket/process tests for partial and oversize frames, timeout,
disconnect, two clients, singleton/stale socket cleanup, explicit transport
intent, sleep lifecycle, and EOF without the UI. Do not add retry/deduplication
infrastructure unless a concrete non-idempotent command requires it.

Gate: both platform builds link and package all dependencies; the service starts
idle; a client controls local playback with no UI; and concurrent gameplay still
passes the measured output gate. No foreground hold protocol is added.

### 4 — Move radio/podcast ownership and reattach the UI

Files: `radio.c/h`, `podcast.c/h`, `module_radio.c`, `module_podcast.c`,
`module_player.c`, `module_common.c`, `settings.c/h`, `resume.c/h`, service
files, and affected `ui_*` modules.

Move radio metadata/rate/reconnect and downloaded podcast progress behind the
owner. Replace UI playback calls with the client facade only after source
ownership is complete. Remove UI `Player_init`/`Player_quit` only then; UI exit
and browsing detach rather than stop. Keep dirty-flag rendering, artwork,
lyrics, visualization, settings, and resume through explicit bounded snapshots
or files. Route media HID to the owner once, avoiding duplicate UI/service
polling.

Gate: start playback, close/reopen the Music Player, browse, launch minarch,
N64, Flycast, Drastic, and PortMaster, and verify uninterrupted music plus
foreground audio. Repeat for radio and podcasts and record rates, PCM state,
underruns, and load against Milestone 1 budgets.

### 5 — Integrate only sleep/wake and shutdown lifecycle

Files: both platform `MinUI.pak/launch.sh`, both Music Player launch scripts,
`workspace/all/common/api.c` sleep/power paths, `nextui/launcher.c` only if
needed, and OSD `toggle_power/set.sh`.

Start the owner once after audio monitoring initialization. Wire sleep entry to
stop decode and close the music stream safely; on wake, wait for routing to be
ready, reopen at the fixed published rate, and restore the user's playing intent
only if it was playing before sleep. Stop synchronously with a bounded deadline
on every poweroff route. Make absent/crashed-owner handling non-blocking.

Do not add launch-loop foreground holds, `musicplayerctl suspend <foreground>`,
resume-on-return, UI-close stop, game/video release commands, or any equivalent
alias. Video remains a separate unresolved policy and is not used as a reason to
change gameplay behavior. Test physical and OSD poweroff, auto-sleep, wake,
owner crash, and nested sleep/poweroff transitions on both platforms.

### 6 — Ship the OSD controls

Files: `skeleton/SYSTEM/osd/common/widgets/app_music/config.json` and
`launch.sh`, required icons, and the four device layouts; add an adapter target
only if Milestone 0 proves it is required. Implement exactly the observed
widget protocol. Bridge previous/play-pause/next to the Music owner and show
bounded title/artist/state. Do not add seek, music-volume, ducking, or a second
owner. Validate idle/error, owner crash, long titles, repeated show/hide, focus
capacity, and all four layouts while gameplay audio and music continue.

## Build and validation commands

No firmware build is part of this planning task. Future implementation should
follow `.dev/BUILD.md` and `.dev/TESTING.md`, build `tg5040` and `tg5050`
sequentially (Music Player's shared `opus_obj/` is not platform-namespaced), and
rebuild all consumers of any `common/api.c` change. The existing focused Music
Player commands are:

```sh
docker run --rm -v "$(pwd)/workspace:/root/workspace" \
  ghcr.io/loveretro/tg5040-toolchain:latest \
  /bin/bash -c 'source ~/.bashrc && cd /root/workspace/all/musicplayer && make PLATFORM=tg5040'

# Repeat sequentially with the tg5050 image and PLATFORM=tg5050.
```

For future OSD validation, assemble each model to a temporary destination with
`scripts/assemble-osd.sh` as documented in `.dev/TESTING.md`; reboot after OSD
deployment because the live tree is an overlay mount. Capture real device
output, not just successful pushes.

## Connected-device evidence

Validated on **Trimui Brick Pro / tg5040**, firmware **1.1.1**, Linux
**4.9.191**, ADB serial `6c000c89c50607e19dd`.

- Live speaker route: `plug -> softvol -> plug -> dmix`, backed by
  `hw:audiocodec,0`, S16_LE stereo at 48000 Hz, period 2048, buffer 8192.
- Luna ran a silent single-stream baseline and two simultaneous
  `aplay -q -D default -f S16_LE -c 2 -r 48000` processes against temporary
  zero PCM. Both held `pcmC0D0p`; the slave was RUNNING at 48 kHz and both
  exited successfully (`RESULTS 0 0`).
- This proves concurrent default ALSA opens on this speaker route only, not
  SDL Music Player plus gameplay, audible continuity, decoder load, hotplug,
  or callback/CPU budgets. Milestone 1 is only partially evidenced.
- No USB DAC or active Bluetooth output was tested.
- Live Brick Pro OSD binary matches the repository: MD5
  `62df0658c51170bc6a9cbace42ddaab4`. Layout and app_music config/launch
  also matched; the music widget is absent from the active layout.
- Luna's static analysis found the app command path opened for writing and
  canvas path opened read-only and mmapped. This is not the internal service
  socket protocol. Retain a distinct adapter/compatible endpoint and establish
  the canvas backing before OSD opens it; configured size is 561600 bytes.
- Directional/button literals were identified, but exact write lengths,
  framing, endpoint type, startup blocking, focus mapping, pixel order and
  rendering still need active verification. String literals alone are not a
  proven command contract. Milestone 0 remains open.
- Active widget testing needs temporary layout/daemon activation with a
  restoration procedure. No layout change, restart, remount or reboot was
  performed in this probe.

Temporary remote probe files were cleaned up. Parent verification confirmed
PCM returned to `closed`, sink stayed `default`/48000, and the live OSD hash
matches. Existing foreground processes, volume and routing were not changed.
Full evidence is retained in the session artifact
`subagent-artifacts/outputs/c5e433db-f419-421a-80b7-c0b2c235a824/hardware/adb-evidence.md`.

## Explicit unresolved hardware gates

The following are unknown in this checkout and remain acceptance gates:

- whether each USB DAC permits simultaneous opens through its actual route;
- whether an active BlueZ `MediaTransport1` route permits concurrent opens,
  its latency, and its rate cap in practice;
- the working shared-output replacement for the known direct USB/BlueALSA
  routes, and the verified scope of their mixer controls;
- the exact audio path, SDL/ALSA behavior, and lifecycle of PortMaster games;
- whether Drastic's closed/private audio implementation can coexist without an
  exclusive device or private mixer behavior;
- whether sink hotplug can be handled by independent process reopens without
  disturbing the other stream;
- the exact amp-pop requirements on both divergent `libmsettings` copies;
- the closed OSD widget protocol and model-specific behavior.

No claim of universal gameplay mixing, routing, or hardware support is valid
until those gates are tested and the measured evidence is recorded. The
confirmed requirement remains: browsing and gameplay must not intentionally
suspend Music Player playback.
