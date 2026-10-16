# Headphone recall after a Brick stack restart

This change addresses intentional `bluetoothd` termination on tg5040. It does not
replace BlueZ link-loss policy or establish that a particular headset will reconnect.

## Source comparison (checked 2026-10-14)

- [NX Redux main c7983f53](https://github.com/mohammadsyuhada/nx-redux/tree/c7983f5398e4657be48a89b02ccb0b24f3f336fc):
  `skeleton/SYSTEM/tg5040/bin/suspend` calls `bt_init.sh stop` before deep sleep
  and `start` after wake. `bt_init.sh` powers off and kills bluetoothd on stop.
  Settings also powers off before invoking stop; moving that operation into the
  existing stop script permits target capture while Connected is still available.
  The OSD already uses the same stop/start script.
- [NextUI main a87e3b56](https://github.com/LoveRetro/NextUI/tree/a87e3b56b8b40b89245c19d47c46254f51779b33):
  the corresponding tg5040 suspend and Bluetooth init scripts also stop and restart
  the daemon. This lifecycle predates this patch.
- [MinUI main dbf89435](https://github.com/shauninman/MinUI/tree/dbf89435a5179788418ff3573042cf9c7e75d9e0):
  no equivalent Bluetooth lifecycle helper was identified in the complete tree.
  Its tg5040 launcher runs `rfkill block bluetooth`. It is ancestry context, not
  evidence for an existing headphone-reconnect implementation.
- [BlueZ current dd9d5ba1 policy.c](https://github.com/bluez/bluez/blob/dd9d5ba1fa4f682b082b1275a8c38523d95a58fd/plugins/policy.c)
  and [the packaged 5.78 policy.c](https://github.com/bluez/bluez/blob/5.78/plugins/policy.c):
  the policy plugin tracks connected auto-connect profiles in the in-memory
  `reconnects` list, handles timeout/local-host-suspend disconnect reasons, and
  frees tracking on plugin exit. Defaults include audio profiles and seven attempts;
  see [5.78 main.conf](https://github.com/bluez/bluez/blob/5.78/src/main.conf).
  A fresh daemon does not recover that prior active-profile list from bonding alone.
  The installed device configuration and actual headset responses were not inspected
  for this commit.

## Reuse and scope

The implementation selectively reuses
[PR #233 at 54272251](https://github.com/mohammadsyuhada/nx-redux/tree/54272251466caade3877b8d527c8411728316ba4):
exact property/MAC checks, bounded CLI execution, volatile state, and a short
post-restart retry window. The old patch series is not applied. The restart-only
path excludes boot recall, temporary pairing, and automatic Disconnect cancellation.
The separate opt-in boot path below reuses the bounded worker. Existing saved
bonds and the controller path are unchanged.

A stop captures exactly one connected, bonded, trusted Audio Sink before adapter
power-off or daemon termination. HID devices, including devices advertising both
HID and Audio Sink, are excluded. Ambiguous or failed capture skips recall. The
whole capture has a six-second budget. Its temporary target is consumed once;
a plain start, live-daemon start, or reboot has no captured target to replay.

A restart worker checks adapter/BlueALSA readiness, rechecks the target, and makes
at most two Connect attempts in a 30-second window. Each command is externally
bounded because bluetoothctl's own timeout does not cover all readiness waits.
The retained child-identity watchdog supports the previously observed firmware
BusyBox without a timeout applet; these compatibility observations are historical,
not a new hardware verification. Readiness checks reduce premature calls but do
not prove audible playback. A timed-out CLI can leave a BlueZ request pending;
there is deliberately no Disconnect cleanup.

The generation token cancels superseded work. The shared lock protects the final
current-generation check and Connect against a cooperating restart; daemon identity
also binds the worker to the stack it was launched for. Shutdown proceeds even when
state preparation or lock acquisition fails. Startup requires that lock and fails
if it cannot be acquired, preventing an old worker from targeting a replacement
daemon. Repair the helper/state failure and retry startup. Restart capture state
stays under /tmp; that path writes no saved pairing or persistent preference.

## Opt-in boot recall

On tg5040, Boot reconnect is Off by default. Enabling it in Bluetooth settings
saves a preferred, bonded and trusted Audio Sink when there is exactly one eligible
connected headset; a later verified Pair or Connect can update that target. HID
controllers, including devices advertising both HID and Audio Sink, are excluded.
Unpair clears a matching target. The preference is stored in
`.userdata/tg5040/bt-boot-audio`; no new bond or temporary pairing is created.

The launcher offers one boot recall opportunity per boot, even if Bluetooth is
Off, the preference is Off, or no target is saved at that moment. Changing the
setting afterward takes effect on the next boot; entering the launcher again
does not schedule another attempt. Manual Bluetooth actions cancel a pending boot
attempt. A connected second headset suppresses recall; an unavailable target
gets at most two Connect attempts within 30 seconds. The worker checks its current
generation, daemon identity, preference, and cancellation under the lifecycle
lock before Connect. It does not automatically Disconnect any device. This boot
path does not change the separate stop/start recall behavior described above.
Cancellation stops subsequent attempts, but a Connect already submitted to BlueZ
may still complete.

The policy and process bounds are host-tested. On 2026-10-15, tg5040 hardware
checks confirmed audible automatic headphone reconnection with the setting On,
no automatic reconnection with it Off, and a responsive system when the saved
headphones were unavailable. These checks do not measure every timing bound or
establish behavior for other headset models.

## Restart hardware acceptance

On the observed tg5040 firmware, BusyBox 1.27.2 `pgrep -x bluetoothd` missed a
running daemon because its match used `/usr/bin/bluetoothd`; `pidof bluetoothd`
found its PID. Daemon selection now requires exactly one PID, exact
`/proc/PID/comm` name `bluetoothd`, and a readable process start time. This
compatibility fix passed Settings off/on and deep-sleep/wake trials with AirPods
Pro on 2026-10-14. The original a3d48994 build failed the toggle trial; acceptance
applies to that build plus this fix. Device evidence records fresh Bluetooth
processes, restored connection, and 97.382 seconds of successful suspend without
reboot. Automatic audible recovery was user-confirmed; audio timing was not
independently measured. Endurance, unavailable-headset restart, and controller
hardware cases remain untested.

## Verification

Run `python3 scripts/tests/bluetooth_reconnect/test_reconnect.py` and
`python3 scripts/tests/bluetooth_reconnect/test_boot.py` from the repository.
The tests cover selection, capture ordering, one-shot consumption, bounds, stale
workers, controller exclusion, shutdown failure paths, boot preference parsing,
first-opportunity consumption, cancellation, and Settings status. They are deterministic
host tests, not a measurement of radio range, headset availability, audible audio,
actual suspend success, or firmware tool compatibility. Those require hardware
acceptance checks; the scoped device results above are separate from this suite.

The Linux-only tests exercise the real process watchdog and fatal lock-file
redirection. In the pinned tg5040 toolchain container, a temporary shim maps
BusyBox `sleep` and `flock` to the container's standard utilities because that
image has no BusyBox binary. This verifies the Linux process/lock logic, not the
installed firmware's applet behavior. macOS skips these Linux-only cases.

The budgets apply to the new capture and recall work. Existing stack initialization
and shutdown commands are not all deadline-bounded. The stale-worker guarantee
covers this recall worker, not an indefinitely paused external/older initializer.
A shutdown failure path executes the existing shutdown sequence; host tests do not
prove a lasting powered-off state in the presence of unrelated concurrent actors.
