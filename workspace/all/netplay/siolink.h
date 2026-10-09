#ifndef SIOLINK_H
#define SIOLINK_H

#include <stdbool.h>
#include <stddef.h>

// gpSP lockstep link cable in USB Cable sessions, over usblink.elf: this
// minarch registers a Unix datagram socket with the daemon (usblink_sio.h),
// which carries each message to the peer as a ULF_SIO frame on the existing
// nxlink endpoints (~0.35 ms round trip vs ~1 ms for TCP over the TUN). No
// threads, nothing blocks: the core's mid-frame poll calls siolink_drain.
// gbalink sends lockstep packets here when siolink_is_up() and delivers what
// arrives here (and over TCP) regardless.
int siolink_start(void);															   // 0 = registered with usblink, handshake runs on drain; -1 = unavailable (IP), nothing left open
bool siolink_is_up(void);															   // our side completed the handshake and the daemon is still there
int siolink_send(const void* buf, size_t len);										   // 0 handed to usblink; -1 not up / too big / socket full (caller uses TCP)
int siolink_drain(void (*deliver)(const void* buf, size_t len, void* ctx), void* ctx); // non-blocking; DATA delivered
void siolink_stop(void);															   // idempotent, never blocks

// Env NX_SIOLINK=0 disables siolink (escape hatch, and the way to test the IP fallback).

#endif
