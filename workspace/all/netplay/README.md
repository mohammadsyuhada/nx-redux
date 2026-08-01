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
| Netplay (frame-sync rollback) | fbneo, fceumm, snes9x, supafaust, picodrive, pcsx_rearmed | 55435/55436 |
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

### gpsp_serial / link_mode

`gpsp_serial` (`auto|disabled|rfu|mul_poke|mul_aw1|...`) is resolved by the
core at ROM load; `auto` picks the right mode from the built-in game table
(FireRed → RFU). The wizard flow passes the HOST's setting in the handshake;
on mismatch the client adopts the host's mode in-band
(`GBALINK_CONNECT_NEEDS_RELOAD` → adopt + persist + core reload + reconnect,
`netplay_boot.c`). The string values are compared verbatim — "auto" on a
FireRed host and "rfu" on the client is a mismatch even though they resolve
to the same hardware.
