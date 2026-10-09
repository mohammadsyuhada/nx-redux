// sio_lockstep.c - see sio_lockstep.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "sio_lockstep.h"

#include <string.h>

static void put16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t* p, uint32_t v) {
	put16(p, (uint16_t)v);
	put16(p + 2, (uint16_t)(v >> 16));
}
static void put64(uint8_t* p, uint64_t v) {
	put32(p, (uint32_t)v);
	put32(p + 4, (uint32_t)(v >> 32));
}
static uint16_t get16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t get32(const uint8_t* p) {
	return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}
static uint64_t get64(const uint8_t* p) {
	return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

// [0..3] "NXLS" [4] type [5] mode [6] ctrl [7] 0 [8..9] seq [10..11] 0 [12..15] word [16..23] cycle
int siols_encode(const SioLsMsg* m, uint8_t* out) {
	memset(out, 0, SIOLS_MSG_SIZE);
	out[0] = 'N';
	out[1] = 'X';
	out[2] = 'L';
	out[3] = 'S';
	out[4] = m->type;
	out[5] = m->mode;
	out[6] = m->ctrl;
	put16(out + 8, m->seq);
	put32(out + 12, m->word);
	put64(out + 16, m->cycle);
	return SIOLS_MSG_SIZE;
}

int siols_decode(const void* buf, size_t len, SioLsMsg* m) {
	const uint8_t* p = (const uint8_t*)buf;
	if (!siols_is_packet(buf, len) || p[4] < SIOLS_START || p[4] > SIOLS_SYNC || p[5] > SIOLS_NORMAL32)
		return -1;
	m->type = p[4];
	m->mode = p[5];
	m->ctrl = p[6];
	m->seq = get16(p + 8);
	m->word = get32(p + 12);
	m->cycle = get64(p + 16);
	return 0;
}

// What a GBA reads when nobody is on the other end of the cable.
static uint32_t no_peer(uint8_t mode) {
	return mode == SIOLS_MULTI ? 0xFFFFu : mode == SIOLS_NORMAL8 ? 0xFFu
																 : 0xFFFFFFFFu;
}

static void send_msg(SioLs* s, const SioLsMsg* m) {
	uint8_t b[SIOLS_MSG_SIZE];
	siols_encode(m, b);
	s->io.send(s->io.ctx, b, sizeof(b));
}

static bool engaged(const SioLs* s) {
	return s->linked && s->ever_xfer && s->now - s->last_xfer < SIOLS_ENGAGE_CYCLES;
}

static int64_t mapped(const SioLs* s, const SioLsMsg* m) {
	return (int64_t)m->cycle + s->offset;
}
static bool bounded(const SioLs* s) {
	return !s->leader && s->epoch_valid && engaged(s);
}
static int64_t bound(const SioLs* s) {
	return (int64_t)s->leader_latest + s->offset + (int64_t)SIOLS_SLACK_CYCLES;
}
static bool at_bound(const SioLs* s) {
	return bounded(s) && (int64_t)s->now >= bound(s);
}

// A follower behind the leader still answers once its game has had a
// transfer's worth of time since the last one: it only has to have run its
// serial IRQ handler, not to have reached the leader's exact cycle. This keeps
// the leader's wait at about one round trip instead of the follower's lag.
static bool start_due(const SioLs* s, const SioLsMsg* m) {
	if (s->leader || !s->answered || (int64_t)s->now >= mapped(s, m))
		return true;
	return s->now - s->last_start_at >= SIOLS_MIN_GAP_CYCLES;
}

static bool can_take(const SioLs* s, bool busy) {
	return !busy && s->inbox_n > 0 && start_due(s, &s->inbox[0]);
}

static void record_wait(SioLs* s, uint64_t us) {
	s->st.xfers++;
	s->st.wait_us_total += us;
	if (us > s->st.wait_us_max)
		s->st.wait_us_max = us;
	s->st.hist[us < 500 ? 0 : us < 1000 ? 1
						  : us < 2000	? 2
						  : us < 5000	? 3
										: 4]++;
}

void siols_init(SioLs* s, const SioLsIo* io) {
	memset(s, 0, sizeof(*s));
	s->io = *io;
}

void siols_start(SioLs* s, bool leader) {
	SioLsIo io = s->io;
	uint64_t now = s->now;
	memset(s, 0, sizeof(*s));
	s->io = io;
	s->now = now;
	s->linked = true;
	s->leader = leader;
	s->next_poll = now;
}

void siols_stop(SioLs* s) {
	s->linked = false;
	s->inbox_n = 0;
	s->epoch_valid = false;
}

void siols_on_packet(SioLs* s, const void* buf, size_t len) {
	SioLsMsg m;
	if (!s->linked || siols_decode(buf, len, &m) != 0)
		return;
	// Only transfer traffic proves the peer answers again: the leader keeps
	// sending SYNCs while it ignores our STARTs (menu, mismatched game).
	if (m.type != SIOLS_SYNC)
		s->broken = false;
	if (m.type == SIOLS_REPLY) {
		if (s->xfer_active && !s->reply_ready && m.seq == s->out.seq) {
			s->reply_ready = true;
			s->reply_word = m.word;
		}
		return;
	}
	if (!s->leader) {
		if (m.cycle > s->leader_latest)
			s->leader_latest = m.cycle;
		// (Re)anchor on a new session, after a bound timeout, or on the first
		// transfer after a quiet spell.
		if (!s->epoch_valid || (m.type == SIOLS_START && !engaged(s))) {
			s->offset = (int64_t)s->now - (int64_t)m.cycle;
			s->leader_latest = m.cycle; // the leader may have gone back (reset, state load)
			s->epoch_valid = true;
		}
	}
	if (m.type != SIOLS_START)
		return;
	if (s->answered && m.seq == s->last_reply.seq) { // our REPLY got lost: answer again
		send_msg(s, &s->last_reply);
		s->st.reanswers++;
		return;
	}
	for (int i = 0; i < s->inbox_n; i++)
		if (s->inbox[i].seq == m.seq)
			return; // re-sent START still queued
	if (s->inbox_n < SIOLS_INBOX)
		s->inbox[s->inbox_n++] = m;
}

void siols_advance(SioLs* s, uint32_t cycles) {
	s->now += cycles;
	if (!s->leader || !engaged(s) || s->now < s->next_sync)
		return;
	SioLsMsg m = {.type = SIOLS_SYNC, .cycle = s->now};
	send_msg(s, &m);
	s->next_sync = s->now + SIOLS_SYNC_CYCLES;
}

static int64_t min64(int64_t a, int64_t b) {
	return a < b ? a : b;
}

uint32_t siols_next_event(const SioLs* s) {
	if (!s->linked)
		return UINT32_MAX;
	int64_t now = (int64_t)s->now;
	int64_t ev = (int64_t)s->next_poll - now;
	if (s->leader && engaged(s))
		ev = min64(ev, (int64_t)s->next_sync - now);
	if (bounded(s))
		ev = min64(ev, bound(s) - now);
	if (!s->leader && s->inbox_n && s->answered) {
		ev = min64(ev, mapped(s, &s->inbox[0]) - now);
		ev = min64(ev, (int64_t)(s->last_start_at + SIOLS_MIN_GAP_CYCLES) - now);
	}
	if (ev < 1)
		ev = 1;
	if (ev > (int64_t)UINT32_MAX - 1)
		ev = (int64_t)UINT32_MAX - 1;
	return (uint32_t)ev;
}

void siols_service(SioLs* s, bool busy) {
	if (!s->linked)
		return;
	// Also poll at once when the link just came into use, so the idle cadence
	// does not delay the first busy-cadence poll by up to SIOLS_IDLE_POLL_CYCLES.
	bool tighten = engaged(s) && s->next_poll > s->now + SIOLS_POLL_CYCLES;
	if (s->now >= s->next_poll || at_bound(s) || tighten) {
		s->io.poll(s->io.ctx);
		s->next_poll = s->now + (engaged(s) ? SIOLS_POLL_CYCLES : SIOLS_IDLE_POLL_CYCLES);
	}
	// Mid-transfer, or with a START to answer, a follower never waits.
	if (busy || !at_bound(s) || can_take(s, false))
		return;
	s->st.bound_waits++;
	uint64_t t0 = s->io.now_us(s->io.ctx);
	for (;;) {
		s->io.sleep_us(s->io.ctx, SIOLS_IDLE_SLEEP_US);
		s->io.poll(s->io.ctx);
		if (!s->linked || !at_bound(s) || can_take(s, false))
			return;
		if (s->io.now_us(s->io.ctx) - t0 >= SIOLS_BOUND_TIMEOUT_US) {
			// Leader went quiet (menu, pause, gone): run free; its next message re-anchors us.
			s->epoch_valid = false;
			s->st.bound_timeouts++;
			return;
		}
	}
}

void siols_initiate(SioLs* s, uint8_t mode, uint8_t ctrl, uint32_t word) {
	s->xfer_active = true;
	s->ever_xfer = true;
	s->last_xfer = s->now;
	s->out = (SioLsMsg){.type = SIOLS_START, .mode = mode, .ctrl = ctrl, .seq = (uint16_t)(s->seq + 1), .word = word};
	if (!s->linked || s->broken) {
		// No peer, or it just stopped answering: finish with "nobody there"
		// now instead of freezing the game for another timeout.
		s->reply_ready = true;
		s->reply_word = no_peer(mode);
		s->waiting = false;
		return;
	}
	s->seq++;
	s->out.cycle = s->leader ? s->now : (uint64_t)((int64_t)s->now - s->offset);
	s->reply_ready = false;
	s->waiting = true;
	send_msg(s, &s->out);
	s->t0_us = s->last_send_us = s->io.now_us(s->io.ctx);
}

uint32_t siols_complete(SioLs* s) {
	if (!s->xfer_active)
		return no_peer(s->out.mode);
	while (!s->reply_ready) {
		if (!s->linked) { // session ended inside our own poll
			s->reply_word = no_peer(s->out.mode);
			break;
		}
		s->io.poll(s->io.ctx);
		if (s->reply_ready)
			break;
		uint64_t t = s->io.now_us(s->io.ctx);
		if (t - s->t0_us >= SIOLS_REPLY_TIMEOUT_US) {
			s->broken = true;
			s->st.timeouts++;
			s->reply_word = no_peer(s->out.mode);
			break;
		}
		if (t - s->last_send_us >= SIOLS_RESEND_US) {
			send_msg(s, &s->out);
			s->last_send_us = t;
			s->st.resends++;
		}
		s->io.sleep_us(s->io.ctx, SIOLS_IDLE_SLEEP_US);
	}
	if (s->waiting)
		record_wait(s, s->io.now_us(s->io.ctx) - s->t0_us);
	s->xfer_active = s->reply_ready = s->waiting = false;
	return s->reply_word;
}

bool siols_take_start(SioLs* s, bool busy, SioLsMsg* out) {
	if (!can_take(s, busy))
		return false;
	*out = s->inbox[0];
	s->inbox_n--;
	memmove(s->inbox, s->inbox + 1, (size_t)s->inbox_n * sizeof(s->inbox[0]));
	// Answered before reaching the leader's time: we were behind, move the
	// anchor up to here. Never the other way, or an ahead follower's bound
	// would ratchet forward every transfer.
	if (!s->leader && mapped(s, out) > (int64_t)s->now)
		s->offset = (int64_t)s->now - (int64_t)out->cycle;
	return true;
}

void siols_reply(SioLs* s, const SioLsMsg* start, uint32_t word) {
	SioLsMsg r = {.type = SIOLS_REPLY, .mode = start->mode, .ctrl = start->ctrl, .seq = start->seq, .word = word, .cycle = s->now};
	send_msg(s, &r);
	s->last_reply = r;
	s->answered = true;
	s->last_start_at = s->now;
	s->ever_xfer = true;
	s->last_xfer = s->now;
	s->st.replies++;
}
