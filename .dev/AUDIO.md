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
time and publishes `/tmp/nx_audio_sink` (`sink=`/`rate=`/`rates=`/`format=`/`card=`, written
atomically via tmp+rename). `rate=` is the single active mixer rate selected
from the probed supported list (the speaker is fixed at 48 kHz). Consumers:

- `AudioMgr_pickRate(int desired)` (`common/audio_manager.h`) — returns the
  published active `rate=` regardless of source preference; fallback is 48 kHz.
  Source-following remains a metadata/preference mode but cannot choose a
  second hardware rate while streams share the mixer.
- `nx_pick_audio_rate()` shell helper for script-launched players.

Policy facts:

- ALSA `plug` stays in the chain as a safety net; the goal is that it never
  has to convert.
- USB routing uses `plug -> nx_usb_dmix -> hw:<card>,0` at the published
  rate/`S16_LE` format, allowing concurrent streams on a USB DAC.
- Bluetooth remains `plug -> bluealsa`; BlueALSA v4.1.0 accepts one PCM
  client, and no kernel snd-aloop/alsaloop workaround is available on the
  target firmware, so BT concurrent sharing remains an explicit blocker.
- BT max-rate capping is applied once at audiomon publish time.
- USB rate lists are published 48000-first. Some USB DACs reject 44.1 kHz
  entirely — probe results are authoritative, don't assume.
- BT routing is driven by BlueZ `MediaTransport1` add/remove, not device
  Connected state (LE-first earbuds connect long before audio is available).
- The Settings → Audio page pokes audiomon with SIGUSR1 to republish. It
  also hosts the Volume item (moved there from the System section).
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
apply no FN overrides.

Mixer paths differ too: tg5050's speaker path is `amixer -c 0 cget
name='DAC Volume'`; tg5040 uses a reversed-mapping `'digital volume'`. The
reversal is **not** a bug: both codecs expose the same 0–63, 1.16 dB/step
control, but tg5040's register counts attenuation (opposite of its TLV
metadata) while tg5050's counts the normal direction — the two mappings land
on the same effective curve.

tg5050 speaker loudness (found + fixed 2026-08-20): the speaker amp is fed
from LINEOUT, and the driver defaults `LINEOUT Gain` to 19 (−18 dB). Stock
raises it at boot (`tinymix set 18 23` in `runtrimui-original.sh`) but our
boot path skips that script, leaving the speaker ~10.5 dB quieter than
tg5040 (whose `LINEOUT volume` defaults to 26 ≈ −7.5 dB). tg5050's
`InitSettings` now sets `LINEOUT Gain` 31 (0 dB), and `SetRawVolume` on
**both platforms** maps percent → digital raw through a perceptual-taper
table (`dB = 36.4·log10(val/100) − 4.6`): 50% ≈ −15 dB instead of −38 dB,
and the top is held 4.6 dB under full scale because both speaker amps
audibly distort with the DAC at 0 dB. tg5040's table is mirrored into
attenuation steps for its reversed register. The Brick's analog stage stays
at its default 26, so it sits ~7.5 dB quieter than the TSPS at the same
dial position.

Deploy note: the **live** library on device is
`/mnt/SDCARD/.system/lib/libmsettings.so` — the layout is flat; a
`.system/<plat>/lib` path is stale and pushes there silently do nothing.
Verify with `grep msettings /proc/$(pidof nextui.elf)/maps`.

On-device lib test recipe: cross-compile a small C harness against the lib in
the toolchain docker image, run with
`LD_LIBRARY_PATH=/mnt/SDCARD/.system/lib`. To replace the `.so` while
processes have it mapped, `adb push` to `<name>.so.new` and `mv` into place —
a same-filesystem rename keeps the old inode alive (a direct push, or a
cross-filesystem `mv` from /tmp, overwrites the mmap'd file in place and can
crash the mapper). Reboot afterwards so everything picks up the new copy.

## FN mode and speaker mute: two flags, one owner each

The former single `settings->mute` flag is split into two independent flags,
each written by exactly one owner:

- `fn_mode` — keymon sets it from the FN switch GPIO only, via `SetFnMode`,
  which applies (and reverts) the whole FN profile: `fn_volume`,
  `fn_brightness`, `fn_colortemperature`, `fn_contrast`, `fn_saturation`,
  `fn_exposure`, the D-pad mode and turbo-fire flags, and the LEDs.
- `speaker_mute` — the OSD Mute widget sets it via `osdctl set mute`; keymon's
  volume keys clear it via `SetSpeakerMute(0)` before they step the volume. It
  only silences output; it touches no display or input setting.

`SetRawVolume()` is the single hardware choke point where both flags meet:
apply the FN volume override first, then `if (settings->speaker_mute) val = 0;`,
then the sink-specific write. Every path that changes the hardware volume runs
through it, so it is the one place to reason about "why is there no sound".

Both flags are transient: the settings host resets `fn_mode` and `speaker_mute`
to 0 at boot, and keymon re-derives `fn_mode` from the GPIO.

Keep the layout in lockstep across **four** files: the two libmsettings copies
above, `workspace/desktop/libmsettings/msettings.c`, and
`workspace/all/common/msettings_shm.h`. Struct versions are tg5040
`SETTINGS_VERSION 12`, tg5050 `3`, desktop `10`; `MSETTINGS_SHM_VERSION`
mirrors 12/3 so a stale shm segment from an older binary is rejected. The LED
config key is `fnLeds`; the loader still accepts the legacy `muteLeds=` on read
so old `minuisettings.txt` files keep working.

## Misc facts

- A stale `~/.asoundrc` under `HOME=/root` can break ALSA `default` for
  root-homed processes; emulators are unaffected because `launch.sh` sets
  `HOME=$USERDATA_PATH`.
- Music player exposes three audio knobs (rate mode / SRC quality / buffer);
  buffer changes reopen the device.
- Savestate loads re-init the audio device — a one-off click there is
  expected, not a bug.
