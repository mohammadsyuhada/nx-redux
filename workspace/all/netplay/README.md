# Minarch netplay (link engines + pre-launch wizard flow)

Since 2026-08-01 all netplay setup happens in the pre-launch wizard
(`netplay.elf`, see `workspace/all/netplay-wizard/`): Y / "Launch with
Netplay" in the game list, pair, then the emulator boots with the session
already connected. There is no in-game netplay UI — quitting the game ends
the session. `netplay_boot.c` turns the wizard's env handoff
(`NETPLAY_ROLE`/`NETPLAY_PEER_IP`/`NETPLAY_MODE`, exported by
`netplay-prelaunch.sh`) into an engine session before the first frame.

Minarch selects one of three link backends per core (`checkCoreLinkSupport`);
N64.pak (standalone mupen64plus, not a minarch core) is a fourth, separate
path that reuses the same pre-launch wizard for rendezvous only:

| Backend | Cores | Ports |
|---|---|---|
| Netplay (frame-sync rollback) | fbneo, fceumm, snes9x, supafaust, picodrive, pcsx_rearmed, swanstation | 55435/55436 |
| GBA Link (gpSP RFU/serial via libretro netpacket) | gpsp | 55437/55438 |
| GB Link (gambatte serial) | gambatte | 56400/56421 |
| N64 core netplay (mupen64plus protocol via on-host `m64p-server.elf`) | mupen64plus (standalone, `N64.pak`) | 55445 (TCP+UDP) |

N64's wizard is rendezvous-only: it just brokers role + peer IP, then
`N64.pak/launch.sh` starts `m64p-server.elf` on the host and passes
`--netplay <ip> 55445 --netplay-player <1|2>` to mupen64plus. Saves are not
rsync'd by the wizard — the mupen64plus core netplay protocol transfers
player 1's save files to the joiner in-band (P1 authoritative). The joiner's
in-game writes are staged to `netplay-data/mupen64plus` (`--set
Core[SaveSRAMPath]=…`) so its real single-player saves stay untouched.

## Gotchas (all hardware-verified 2026-08-01, Brick ↔ Smart Pro S)

### GBA Link heartbeats — why clients echo them

The game generates NO wireless (RFU) traffic until its wireless features are
actually engaged (e.g. Pokémon FireRed only talks to the adapter once you
reach the Union Room attendant). With boot-time pairing, that idle stretch
routinely exceeds `GBALINK_CONNECTION_TIMEOUT_MS` (60 s). The host sends
`CMD_HEARTBEAT` every 500 ms which keeps the CLIENT alive, but originally
nothing ever flowed client→host, so the HOST's receive-timeout starved on
pure silence and silently dropped the link (back to LISTEN, no UI) before the
players ever met in-game. Fix: the client echoes each host heartbeat
(`gbalink.c`, `CMD_HEARTBEAT` branch) — one echo per 500 ms, no
amplification. Timeout disconnects also log now; they used to be invisible.

### Cloned saves break Pokémon wireless discovery

Two devices running byte-identical saves present identical Trainer ID/SID —
the games will NEVER see each other in the Union Room even over a perfectly
healthy link (discovery dedupes / a trainer cannot pair with itself). The
transport looks fine (TCP ESTABLISHED, heartbeats flowing) while the game
shows an empty room. Use distinct saves on the two devices.

### Cold-boot for netplay; don't resume states

A save state snapshots the emulated link hardware's state from a moment when
no session existed; resuming it into a fresh session is a desync source
(same gotcha class as the DC switcher-resume note). The auto-resume state
also silently overrides a swapped `.srm` — if you replace a save file to get
a distinct trainer, remove/rename the matching `.state.auto` too or the old
trainer comes back.

### Sticks travel with the buttons

Each lockstep input packet carries the player's buttons and both sticks (left
X/Y, right X/Y; `InputPacket`, `NETPLAY_PROTOCOL_VERSION` 3), and the frame
buffer keeps them per player: host sticks on port 0, client sticks on port 1
(`netplay_port_analog`). Before protocol 3 only buttons were sent, so the host
fed its own stick to its core while the client read 0 there, and any stick
movement in an analog game (DualShock on PS/PSX) desynced the session. The
packet format changed, so the wizard's HELLO went to version 2 as well: a
device on an older build is refused at pairing ("different netplay version")
instead of stalling mid-game.

### Different titles: the joiner's override

The wizard's only game gate compares ROM file names (normalized: tags in
()/[] dropped, non-alphanumerics dropped, lowercased) in the HELLO handshake.
Sister versions (FireRed/LeafGreen, Ruby/Sapphire) normalize differently, so
the joiner is prompted (`wiz_client_confirm_other_game`) and, on A, sends
`HELLO 2 <game> client any`; a host that sees `any` skips its name check. On
hotspot the joiner listens ~2.5 s for the host's discovery broadcast to learn
the title before connecting (`wiz_client_hotspot_peek`); silence falls back
to the plain gate. That peek only works because `NET_sendDiscoveryBroadcast`
sends to the interface's subnet-directed address (10.0.0.255): the hotspot
host has no default route (wlan0 was cycled for hostapd), so the old limited
broadcast to 255.255.255.255 failed with ENETUNREACH and never left the host
— on hotspot the prompt silently never appeared. Nothing below the wizard re-checks names or CRCs: gbalink
only compares `gpsp_serial` strings, netplay.c/gblink.c compare nothing. An
old host ignores the token (its fifth HELLO field fails `%d`) and rejects as
before; an old client never sends it.

### gpsp_serial / link_mode

`gpsp_serial` (`auto|disabled|rfu|mul_poke|mul_aw1|...`) is resolved by the
core at ROM load; `auto` picks the right mode from the built-in game table
(FireRed → RFU). The wizard flow passes the HOST's setting in the handshake;
on mismatch the client adopts the host's mode in-band
(`GBALINK_CONNECT_NEEDS_RELOAD` → adopt + persist + core reload + reconnect,
`netplay_boot.c`). The string values are compared verbatim — "auto" on a
FireRed host and "rfu" on the client is a mismatch even though they resolve
to the same hardware.

## USB cable link

The wizard's first connection mode, **USB Cable** (`NETPLAY_MODE=usb`), runs
the same session over a plain USB-C ↔ USB-C data cable: one device's
**second (top) port** to the other device's **main (bottom) port**, either
way round. The top port is a host-only controller (`ehci1`/`ohci1`); the
bottom port is the OTG/gadget port adb also uses. Which end is USB host has
nothing to do with which end is netplay host — the role menu decides that.
2 players only (one cable; the host clamps `max_players` to 2). Hardware-verified
2026-10-08: Brick ↔ Smart Pro S end-to-end in both cable directions; Brick Pro
shares the tg5040 build with Brick and was link-tested in the spike (`ffs.net`
beside adb, TUN link, ~1 ms ping as USB host to a Brick).

Measured over the cable: ~1 ms RTT, p99 ≈ 1.5 ms, max ≈ 2.6 ms, 0 loss
(Wi-Fi to a router: p99 73 ms, max 189 ms, 4 % loss). Frame-sync netplay and
the wizard's rsync save sync run over it unchanged (played: SNES Contra III,
arcade Metal Slug, FC Bomberman II, with save sync).

```
 wizard (netplay.elf)                 game (minarch / N64 / DC)
   │ start/stop + state file             │  plain TCP/UDP to peer IP
   ▼                                     ▼
 usblink.elf daemon  ──── TUN nxlink0 (10.99.0.1 ↔ 10.99.0.2, point-to-point)
   ├─ gadget side: FunctionFS ffs.net on main port (bulk OUT/IN)
   └─ host side:   usbfs on second port, claims the peer's ffs.net interface
```

`usblink.elf` (`workspace/all/usblink/`, installed next to `netplay.elf`) runs
the same on both handhelds and does not know which end of the cable it is on:
it always exposes a vendor interface (class 0xff, subclass 0x4e, "nxlink") on
its gadget port and, at the same time, scans its host port for that interface
every 250 ms. The USB-host side sends `HELLO{ver}` (250 ms until linked, then
1 s keepalives); the gadget side answers `HELLO_ACK{ver}`. A version mismatch
is `USBLINK_ERROR=version` ("Both devices need the same NXRedux version.").
If the gadget cannot attach (stock gadget not bound to a UDC, configfs or
FunctionFS refusing), the daemon logs it and runs **host-only**: it publishes
`down`, scans its host port and links when the peer's gadget appears there.
3 s without a frame, or `ENODEV`/`ESHUTDOWN`/`EPROTO`/`ENOENT` on the host side
(a real cable pull reports `EPROTO`), drops the link
and the address; scanning continues, so a re-plug relinks (~1 s, same
addresses). Everything above the TUN is plain IP — nothing in minarch,
netplay or the link backends knows about USB.

- **Addressing**: USB-host side `10.99.0.1`, USB-gadget side `10.99.0.2`, /30,
  MTU 1500. Point-to-point means no subnet broadcast: in usb mode the host's
  discovery packets go unicast to the peer (`NET_sendDiscoveryTo`), and the
  client connects to `NETPLAY_PEER_IP` = `USBLINK_PEER_IP` (the hotspot
  "known host address" arm). `netplay_boot.c` needs nothing special.
- **CLI**: `start` (daemonize, return once the daemon publishes its first
  state — not once linked), `stop` (SIGTERM, ≤ 2 s, SIGKILL, then **repair**;
  always exit 0, safe when nothing is up), `status` (print the state file),
  `run` (foreground, ignores SIGHUP for adb debugging).
- **Files**: `/tmp/usblink.pid`, `/tmp/usblink.state` (shell-sourceable,
  atomic rename: `USBLINK_LINK=up|down|error`, `USBLINK_SIDE=host|device|`,
  `USBLINK_LOCAL_IP`, `USBLINK_PEER_IP`, `USBLINK_ERROR` = `version` (peer
  protocol mismatch, while running), `tun` or `thread` (startup failures; the
  daemon exits). A gadget failure is not an error value — see host-only above.
  A startup error makes `start` exit 1, which the wizard shows as "USB link
  could not start."; an error state read while waiting shows "USB link
  unavailable on this device." for every value but `version`),
  `/tmp/usblink.udc`
  (the UDC name saved before the rebind; proves our attach unbound it).
  Log: `$LOGS_PATH/usblink.txt`, else `/tmp/usblink.log`.
- **Teardown**: `wiz_teardown()` runs `usblink.elf stop`; `--cleanup` and the
  wizard's start-up heal also stop it whenever `/tmp/usblink.pid` exists (live
  or not — a SIGKILLed daemon's leftovers are exactly what `stop` repairs).

### The gadget-unlink invariant

`usblink` adds `functions/ffs.net` next to `ffs.adb` in configfs gadget `g1`
(UDC unbind → symlink into `configs/c.1` → rebind; adb survives, dropping for
1–3 s). **The function MUST be unlinked from `c.1` before the process's ep0
closes**: a linked FunctionFS function whose ep0 is gone makes the whole
gadget fail to bind → adb dead. The daemon detaches the gadget first on every
exit path (SIGTERM/SIGINT/SIGHUP, fatal error). SIGKILL can't be caught: the
kernel unbinds the gadget and adb stays down until `usblink.elf stop` (or the
next daemon start) runs `usblink_gadget_repair()` — unlink `ffs.net`, rebind
the saved UDC, unmount `/dev/usb-ffs/net`. A daemon killed mid-game freezes
the game, then the in-game menu shows (the normal netplay disconnect).

### The role-node trap

**Never read** `/sys/devices/platform/soc*/usbc0/usb_host` (tg5050:
`soc@3000000/10.usbc0/`), nor `usb_device`, `usb_null` or `usb_otg` in the same
directory. These are trigger nodes: a plain `cat` flips the port role —
`usb_host` puts the main port into host mode and kills adb at once. On the
Smart Pro S the host role **latched across reboots** (no adb, no charging
from a PC). Only `otg_role` is safe to read. Nothing in usblink touches the
role nodes; don't `grep -r` or tab-complete through that directory.

Recovery without a shell: mount the SD card on a computer and create
`.userdata/tg5050/auto.sh` containing

```sh
echo usb_device > /sys/devices/platform/soc@3000000/10.usbc0/otg_role
```

One boot clears the latch; delete `auto.sh` afterwards.

### tg5050 `tun.ko`

The tg5040 kernel (4.9.191) has TUN built in; the stock tg5050 kernel
(5.15.147) does not. We ship `SYSTEM/tg5050/lib/modules/tun.ko`, which the
daemon loads with `finit_module` when `/dev/net/tun` is missing (path:
`$SYSTEM_PATH/lib/modules/tun.ko`) and leaves loaded until the next boot.
A load failure is `USBLINK_ERROR=tun` (a startup error: "USB link could not
start."). The module is mainline `drivers/net/tun.c` from linux-5.15.147
(tarball sha256-checked) built as an external module against the device
config committed as `workspace/tg5050/other/kernel/config-5.15.147`; the
kernel has no MODVERSIONS or signing, so only the vermagic
(`5.15.147 SMP preempt mod_unload aarch64`) must match and the script checks
it. Build: `make build-prebuilt PLATFORM=tg5050 PREBUILT=tun-ko` (script
`workspace/all/prebuilts/tun-ko.sh`, part of `build-prebuilts` for tg5050).
GPL-2.0 notice: `licenses/linux-tun.txt`.

### Power

The host port supplies VBUS: the device on the **top-port** end powers and
charges the other device for the whole session. Accepted for v1 (no draw
limit).
