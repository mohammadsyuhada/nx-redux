// sio_lockstep.h - GBA link-cable lockstep between two emulator instances.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Compiled into gpSP by all/cores/patches/gpsp/004-sio-lockstep.patch
// (nx_sio_lockstep.c includes sio_lockstep.c) and unit-tested on the host by
// tests/run_tests.sh. No gpSP or frontend dependencies: emulated time comes in
// through siols_advance(), wall time, transport and polling through SioLsIo.
//
// The side that starts a transfer (MULTI parent, NORMAL internal clock) sends
// START{seq, mode, word, cycle}, keeps emulating for the transfer's normal
// duration, then waits for REPLY{seq, word} (siols_complete). The other side
// answers when it gets there (siols_take_start + siols_reply). Netplay client
// 0 is the timeline leader: while the link is in use it sends SYNC{cycle}
// every SIOLS_SYNC_CYCLES and client 1 may run at most SIOLS_SLACK_CYCLES past
// the leader's latest known time, so the follower's registers are read close
// to where a real cable would read them.
#ifndef SIO_LOCKSTEP_H
#define SIO_LOCKSTEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIOLS_MSG_SIZE 24
#define SIOLS_FRAME_CYCLES 280896u						   // one GBA frame
#define SIOLS_SYNC_CYCLES (SIOLS_FRAME_CYCLES / 8)		   // leader SYNC cadence while the link is in use
#define SIOLS_SLACK_CYCLES (SIOLS_FRAME_CYCLES / 4)		   // how far the follower may run past the leader
#define SIOLS_MAX_SPACING_CYCLES (SIOLS_FRAME_CYCLES * 2u) // wider leader gaps: a new burst, answered at once
#define SIOLS_POLL_CYCLES 2048u							   // receive poll cadence while the link is in use
#define SIOLS_IDLE_POLL_CYCLES (SIOLS_FRAME_CYCLES / 16)   // ... and while it is idle
#define SIOLS_ENGAGE_CYCLES (SIOLS_FRAME_CYCLES * 120u)	   // "in use" = a transfer in the last ~2 s
#define SIOLS_REPLY_TIMEOUT_US 500000u					   // then the peer reads as absent
#define SIOLS_RESEND_US 20000u							   // START re-sent while waiting (lost frame)
#define SIOLS_BOUND_TIMEOUT_US 100000u					   // leader silent this long: follower runs free
#define SIOLS_IDLE_SLEEP_US 50u
#define SIOLS_INBOX 8

enum { SIOLS_START = 1,
	   SIOLS_REPLY = 2,
	   SIOLS_SYNC = 3 };
enum { SIOLS_MULTI = 0,
	   SIOLS_NORMAL8 = 1,
	   SIOLS_NORMAL32 = 2 };

typedef struct {
	uint8_t type, mode, ctrl; // ctrl: SIOCNT bits 0-1 of the starter (baud / clock), for the responder's duration
	uint16_t seq;
	uint32_t word;
	uint64_t cycle; // sender's timeline (follower-initiated: mapped to the leader's)
} SioLsMsg;

typedef struct {
	void* ctx;
	void (*send)(void* ctx, const void* buf, size_t len);
	void (*poll)(void* ctx); // frontend delivers what arrived -> siols_on_packet (re-entrant)
	uint64_t (*now_us)(void* ctx);
	void (*sleep_us)(void* ctx, unsigned us);
} SioLsIo;

typedef struct {
	uint32_t xfers, replies, timeouts, resends, reanswers, bound_waits, bound_timeouts;
	uint64_t wait_us_total, wait_us_max;
	uint32_t hist[5]; // initiator wait: <0.5, <1, <2, <5, >=5 ms
} SioLsStats;

typedef struct {
	SioLsIo io;
	bool linked, leader, broken;
	uint64_t now; // local emulated cycles
	bool ever_xfer;
	uint64_t last_xfer;
	// initiator
	bool xfer_active, reply_ready, waiting;
	uint16_t seq;
	uint32_t reply_word;
	SioLsMsg out;
	uint64_t t0_us, last_send_us;
	// leader
	uint64_t next_sync;
	// follower
	bool epoch_valid;
	int64_t offset; // follower cycle = leader cycle + offset
	uint64_t leader_latest;
	// responder
	bool answered;
	uint64_t last_start_at;	   // our timeline when we answered the last START
	uint64_t last_start_cycle; // ... and that START's leader time
	SioLsMsg last_reply;
	SioLsMsg inbox[SIOLS_INBOX];
	int inbox_n;
	uint64_t next_poll;
	SioLsStats st;
} SioLs;

// 'N','X','L','S' + exact size: what gbalink routes/delivers as lockstep traffic.
static inline bool siols_is_packet(const void* buf, size_t len) {
	const uint8_t* p = (const uint8_t*)buf;
	return buf && len == SIOLS_MSG_SIZE && p[0] == 'N' && p[1] == 'X' && p[2] == 'L' && p[3] == 'S';
}

int siols_encode(const SioLsMsg* m, uint8_t* out);
int siols_decode(const void* buf, size_t len, SioLsMsg* m);
void siols_init(SioLs* s, const SioLsIo* io);
void siols_start(SioLs* s, bool leader);
void siols_stop(SioLs* s);
void siols_on_packet(SioLs* s, const void* buf, size_t len);
void siols_advance(SioLs* s, uint32_t cycles);
uint32_t siols_next_event(const SioLs* s);
void siols_service(SioLs* s, bool busy);
void siols_initiate(SioLs* s, uint8_t mode, uint8_t ctrl, uint32_t word);
uint32_t siols_complete(SioLs* s);
bool siols_take_start(SioLs* s, bool busy, SioLsMsg* out);
void siols_reply(SioLs* s, const SioLsMsg* start, uint32_t word);
// Forget a transfer we started without waiting for its REPLY (savestate load:
// that START was never sent in this session, so waiting would only time out).
void siols_abort_xfer(SioLs* s);

#endif
