# Audio Architecture

Output routing, rate negotiation, and the resampling traps specific to these
devices.

## The core constraint: dmix is locked at 48 kHz

Both platforms' `/etc/asound.conf` route `default` → plug → softvol → plug →
**dmix locked at 48000 Hz**. No `defaults.pcm.rate_converter` is configured,
so any stream opened at another rate goes through alsa-lib's built-in
**linear interpolator** — audibly gritty. This single fact explains most audio
distortion bugs here:

- libretro cores sound clean because minarch opens SDL audio with
  `SDL_AUDIO_ALLOW_ANY_CHANGE` and adapts to the obtained 48 kHz — one good
  resample (libsamplerate), no ALSA conversion.
- mupen64plus-audio-sdl hardcoded 44100 → constant harsh audio (fixed, below).
- `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE` does **not** discover the dmix rate —
  the ALSA plug layer accepts any rate, so negotiation never fails and never
  learns the slave rate. You must open 48 kHz explicitly.

**Ground truth for "no silent resample":**
`cat /proc/asound/card0/pcm0p/sub0/hw_params` while playing — the rate shown
is what the hardware path actually runs at.

## audiomon and rate negotiation

`workspace/all/audiomon` is the routing daemon. It probes each sink at hotplug
time and publishes `/tmp/nx_audio_sink` (`sink=`/`rates=`/`card=`, written
atomically via tmp+rename). Consumers:

- `AudioMgr_pickRate(int desired)` (`common/audio_manager.h`) — exact match
  wins, else nearest listed rate; fallback is `MIN(desired, 48000)`.
- `nx_pick_audio_rate()` shell helper for script-launched players.

Policy facts:

- ALSA `plug` stays in the chain as a safety net; the goal is that it never
  has to convert.
- BT max-rate capping is applied once at audiomon publish time.
- USB rate lists are published 48000-first. Some USB DACs reject 44.1 kHz
  entirely — probe results are authoritative, don't assume.
- BT routing is driven by BlueZ `MediaTransport1` add/remove, not device
  Connected state (LE-first earbuds connect long before audio is available).
- The Settings → Audio page pokes audiomon with SIGUSR1 to republish.
- **Never hot-restart audiomon manually** — it cannot reconcile pre-existing
  routing; reboot instead.
- mediaplayer's external `ffplay` gets `-af aresample=<pickRate>`; the engine
  restarts ffplay on sink change, which makes it hotplug-safe for free.

## The N64 / standalone-emulator audio patch

`workspace/all/other/mupen64plus/mupen64plus-audio-sdl.patch` — the reference
for fixing audio in any SDL-audio standalone emulator here:

1. `OUTPUT_FREQUENCY` config param, default 48000 (0 = upstream auto) — kills
   the ALSA linear resample.
2. Dynamic rate control in the callback (±0.5% resample-ratio nudge toward a
   buffer target) — without it clock drift pins the buffer and drops a chunk
   (crackle) every ~15 s. The core's `AUDIO_SYNC=True` is **not** the answer:
   its estimator regulates the real cushion down to ~1 callback and multiplies
   underruns; keep it False.
3. Partial fill on underrun (play what's available, zero the tail) instead of
   a full silence callback.
4. `SCHED_FIFO` for the audio callback thread, set via
   `pthread_setschedparam` *from the callback itself* — SDL's
   `SDL_THREAD_PRIORITY_POLICY` env hint does not work on the device's SDL
   2.30.8. Needs `-lpthread`.
5. Prime-to-target before unpause (one short silent prime instead of startup
   stutter).

Generous buffering matters because production is bursty: a target of ~256 ms
operating level took a 4-minute run from 62 underruns + 16 drops to 1/0.
The same 44.1-vs-48 fix is applied in flycast's `audiobackend_sdl2`
(flycast.patch).

## libmsettings (volume/brightness) divergence

`workspace/tg5040/libmsettings/msettings.c` and
`workspace/tg5050/libmsettings/msettings.c` are **independently maintained
copies that have drifted** — when touching mute/FN, volume, or display logic,
diff the same function across both platform copies before concluding
anything; a fix verified on one platform can silently miss a second missing
guard on the other. Known deliberate difference: tg5050's display setters
apply no mute overrides.

Mixer paths differ too: tg5050's speaker path is `amixer -c 0 cget
name='DAC Volume'`; tg5040 uses a reversed-mapping `'digital volume'`.

On-device lib test recipe: cross-compile a small C harness against the lib in
the toolchain docker image, run with
`LD_LIBRARY_PATH=/mnt/SDCARD/.system/<plat>/lib`. Reboot after replacing the
`.so` — a cross-filesystem `mv` from /tmp overwrites the mmap'd file in place.

## Misc facts

- A stale `~/.asoundrc` under `HOME=/root` can break ALSA `default` for
  root-homed processes; emulators are unaffected because `launch.sh` sets
  `HOME=$USERDATA_PATH`.
- Music player exposes three audio knobs (rate mode / SRC quality / buffer);
  buffer changes reopen the device.
- Savestate loads re-init the audio device — a one-off click there is
  expected, not a bug.
