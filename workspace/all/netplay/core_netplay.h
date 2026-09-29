// core_netplay.h - a netplay session the core runs itself.
//
// flycast's GGPO rollback netplay lives inside the core: the pak's launcher
// runs the pre-launch wizard, then hands the session to the core in NX_GGPO_*
// environment variables and tells minarch with NX_CORE_NETPLAY=1. minarch runs
// no link engine of its own for it; it only has to behave like a netplay
// frontend while the session lasts (Multiplayer_isActive: no save states,
// fast-forward, rewind or reset; MENU offers "Leave netplay?" only).
//
// Libc only, so the host unit test links it directly.

#ifndef CORE_NETPLAY_H
#define CORE_NETPLAY_H

#include <stdint.h>

// How long a player may sit in the leave dialog before the session ends. The
// other player's game waits meanwhile.
#define CORE_NETPLAY_LEAVE_GRACE_MS 20000
// Peer silence after which the core ends the session (passed to the core as
// NX_GGPO_DISCONNECT_MS). Longer than the grace, so the dialog's own timeout
// always comes first.
#define CORE_NETPLAY_DISCONNECT_MS 25000

// A player who leaves tells the other device at once ("bye" over UDP, sent a
// few times), so its game ends now rather than after the disconnect timeout;
// the timeout still covers a lost connection, where no goodbye arrives.
#define CORE_NETPLAY_BYE_PORT 55442 // after the wizard's 55440/55441
#define CORE_NETPLAY_BYE_MSG "NXREDUX-NETPLAY-BYE"

// Reads NX_CORE_NETPLAY (and the peer, NX_GGPO_SERVER); when set, also exports
// NX_GGPO_DISCONNECT_MS for the core (call before the core loads the game).
// Clears any ended state.
void CoreNetplay_initFromEnv(void);
// True while a core-run session is live (set and not yet ended).
int CoreNetplay_isActive(void);
// The core gave up on the session (peer gone): remembered so minarch can say
// so before quitting. The session is no longer active afterwards.
void CoreNetplay_markEnded(void);
int CoreNetplay_hasEnded(void);
// Whole seconds left in the leave dialog opened at start_ms (SDL ticks),
// rounded up, 0 once the grace is over. Tick wraparound safe.
int CoreNetplay_leaveSecondsLeft(uint32_t start_ms, uint32_t now_ms);

// The goodbye channel. Open binds the UDP port (0 ok, -1 when no session or
// the socket failed: the timeout then remains the only signal). Send tells
// the peer at port that this side is leaving; Poll (non-blocking, call every
// frame) is 1 once when the peer's goodbye arrived.
int CoreNetplay_byeOpen(uint16_t port);
void CoreNetplay_byeSend(uint16_t port);
int CoreNetplay_byePoll(void);
void CoreNetplay_byeClose(void);

#endif
