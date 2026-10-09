// Host-compiled tests for sio_lockstep (fake wall clock, fake transport).
// Build & run: workspace/all/cores/gpsp/tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sio_lockstep.h"

typedef struct Fake Fake;
struct Fake {
	uint64_t us;					  // fake wall clock, advanced only by sleep_us
	uint8_t out[512][SIOLS_MSG_SIZE]; // sent, not yet pumped to the peer
	int nout, total_sent, polls;
	void (*hook)(Fake* f); // runs on every poll: "the other device"
};

static Fake fa, fb;
static SioLs A, B;
static int hook_at;

static uint64_t f_now(void* c) {
	return ((Fake*)c)->us;
}
static void f_sleep(void* c, unsigned us) {
	((Fake*)c)->us += us;
}
static void f_send(void* c, const void* b, size_t n) {
	Fake* f = c;
	assert(n == SIOLS_MSG_SIZE);
	if (f->nout < 512)
		memcpy(f->out[f->nout++], b, n);
	f->total_sent++;
}
static void f_poll(void* c) {
	Fake* f = c;
	f->polls++;
	if (f->hook)
		f->hook(f);
}

static void setup(void) {
	memset(&fa, 0, sizeof(fa));
	memset(&fb, 0, sizeof(fb));
	SioLsIo ia = {&fa, f_send, f_poll, f_now, f_sleep}, ib = {&fb, f_send, f_poll, f_now, f_sleep};
	siols_init(&A, &ia);
	siols_init(&B, &ib);
}

static void inject(SioLs* s, SioLsMsg m) {
	uint8_t b[SIOLS_MSG_SIZE];
	siols_encode(&m, b);
	siols_on_packet(s, b, sizeof(b));
}

static void pump(Fake* from, SioLs* to) {
	for (int i = 0; i < from->nout; i++)
		siols_on_packet(to, from->out[i], SIOLS_MSG_SIZE);
	from->nout = 0;
}

static SioLsMsg last_sent(Fake* f) {
	SioLsMsg m;
	assert(f->nout > 0 && siols_decode(f->out[f->nout - 1], SIOLS_MSG_SIZE, &m) == 0);
	return m;
}

static void test_codec(void) {
	SioLsMsg m = {.type = SIOLS_START, .mode = SIOLS_NORMAL32, .ctrl = 3, .seq = 0xBEEF, .word = 0xDEADBEEF, .cycle = 0x0123456789ABCDEFull}, r;
	uint8_t b[SIOLS_MSG_SIZE];
	assert(siols_encode(&m, b) == SIOLS_MSG_SIZE);
	assert(b[0] == 'N' && b[1] == 'X' && b[2] == 'L' && b[3] == 'S');
	assert(siols_is_packet(b, sizeof(b)));
	assert(siols_decode(b, sizeof(b), &r) == 0);
	assert(r.type == m.type && r.mode == m.mode && r.ctrl == 3 && r.seq == 0xBEEF && r.word == 0xDEADBEEF && r.cycle == m.cycle);
	assert(!siols_is_packet(b, sizeof(b) - 1) && siols_decode(b, sizeof(b) - 1, &r) == -1);
	b[4] = 9;
	assert(siols_decode(b, sizeof(b), &r) == -1); // unknown type
	b[4] = SIOLS_START;
	b[5] = 7;
	assert(siols_decode(b, sizeof(b), &r) == -1);			 // unknown mode
	uint8_t poke[SIOLS_MSG_SIZE] = {0x4d, 0x50, 0x4b, 0x31}; // gpSP's own "MPK1" traffic
	assert(!siols_is_packet(poke, sizeof(poke)));
}

static void test_unlinked_completes_at_once(void) {
	setup();
	assert(siols_next_event(&A) == UINT32_MAX);
	siols_initiate(&A, SIOLS_MULTI, 3, 0x1234);
	assert(siols_complete(&A) == 0xFFFF);
	assert(fa.total_sent == 0 && fa.us == 0 && A.st.xfers == 0);
}

static void hook_reply_on_3rd_poll(Fake* f) {
	if (f->polls != 3)
		return;
	SioLsMsg s = last_sent(&fa);
	inject(&A, (SioLsMsg){.type = SIOLS_REPLY, .mode = s.mode, .seq = s.seq, .word = 0xB9A0});
}

static void test_leader_transfer(void) {
	setup();
	siols_start(&A, true);
	siols_advance(&A, 1000);
	assert(fa.total_sent == 0); // no link use yet: no SYNC
	fa.hook = hook_reply_on_3rd_poll;
	siols_initiate(&A, SIOLS_MULTI, 3, 0x8FFF);
	SioLsMsg s = last_sent(&fa);
	assert(s.type == SIOLS_START && s.seq == 1 && s.word == 0x8FFF && s.cycle == 1000 && s.ctrl == 3);
	assert(siols_complete(&A) == 0xB9A0);
	assert(fa.polls == 3 && fa.us == 100);
	assert(A.st.xfers == 1 && A.st.timeouts == 0 && A.st.hist[0] == 1);
}

static void test_timeout_marks_broken(void) {
	setup();
	siols_start(&A, true);
	siols_initiate(&A, SIOLS_NORMAL32, 0, 0x11223344);
	assert(siols_complete(&A) == 0xFFFFFFFFu);
	assert(fa.us == SIOLS_REPLY_TIMEOUT_US);
	assert(A.broken && A.st.timeouts == 1 && A.st.hist[4] == 1);
	assert(A.st.resends == SIOLS_REPLY_TIMEOUT_US / SIOLS_RESEND_US - 1); // re-sent while waiting
	// Broken: the next transfer gives up at once instead of freezing the game again.
	uint64_t t = fa.us;
	int sent = fa.total_sent;
	siols_initiate(&A, SIOLS_MULTI, 3, 1);
	assert(siols_complete(&A) == 0xFFFF && fa.us == t && fa.total_sent == sent);
	// Any packet from the peer repairs it.
	inject(&A, (SioLsMsg){.type = SIOLS_SYNC, .cycle = 5});
	assert(!A.broken);
}

static void hook_stop_on_2nd_poll(Fake* f) {
	if (f->polls == 2)
		siols_stop(&A);
}

static void test_stop_mid_wait(void) {
	setup();
	siols_start(&A, true);
	fa.hook = hook_stop_on_2nd_poll;
	siols_initiate(&A, SIOLS_MULTI, 3, 7);
	assert(siols_complete(&A) == 0xFFFF);
	assert(fa.us < 1000 && A.st.timeouts == 0);
}

static void test_stale_reply_ignored(void) {
	setup();
	siols_start(&A, true);
	siols_initiate(&A, SIOLS_MULTI, 3, 1); // seq 1
	inject(&A, (SioLsMsg){.type = SIOLS_REPLY, .seq = 7, .word = 0x99});
	assert(!A.reply_ready);
	inject(&A, (SioLsMsg){.type = SIOLS_REPLY, .seq = 1, .word = 0x42});
	assert(siols_complete(&A) == 0x42 && fa.polls == 0);
}

static void test_follower_answers_and_dedupes(void) {
	setup();
	siols_start(&B, false);
	siols_advance(&B, 50000);
	inject(&B, (SioLsMsg){.type = SIOLS_START, .mode = SIOLS_MULTI, .ctrl = 3, .seq = 1, .word = 0x8FFF, .cycle = 900000});
	SioLsMsg st;
	assert(siols_take_start(&B, false, &st)); // first START of a session: answered at once
	assert(st.word == 0x8FFF && st.seq == 1 && B.offset == 50000 - 900000);
	siols_reply(&B, &st, 0xB9A0);
	SioLsMsg r = last_sent(&fb);
	assert(r.type == SIOLS_REPLY && r.seq == 1 && r.word == 0xB9A0);
	// The leader re-sent it (our REPLY was lost): same answer again, not a second transfer.
	int n = fb.total_sent;
	inject(&B, (SioLsMsg){.type = SIOLS_START, .mode = SIOLS_MULTI, .ctrl = 3, .seq = 1, .word = 0x8FFF, .cycle = 900000});
	assert(fb.total_sent == n + 1 && last_sent(&fb).word == 0xB9A0);
	assert(!siols_take_start(&B, false, &st));
}

static void test_follower_late_answers_after_min_gap(void) {
	test_follower_answers_and_dedupes(); // B: now 50000, answered at 50000, offset -850000
	SioLsMsg st;
	inject(&B, (SioLsMsg){.type = SIOLS_START, .mode = SIOLS_MULTI, .ctrl = 3, .seq = 2, .word = 2, .cycle = 931000});
	siols_advance(&B, 1000);
	assert(!siols_take_start(&B, false, &st)); // mapped 81000 not reached, gap 1000
	assert(siols_next_event(&B) <= SIOLS_MIN_GAP_CYCLES - 1000);
	siols_advance(&B, SIOLS_MIN_GAP_CYCLES - 1000);
	assert(siols_take_start(&B, false, &st) && st.seq == 2);
	assert(B.offset == (int64_t)(50000 + SIOLS_MIN_GAP_CYCLES) - 931000); // anchor caught up
	siols_reply(&B, &st, 2);
	// Never while our own transfer is still running.
	inject(&B, (SioLsMsg){.type = SIOLS_START, .mode = SIOLS_MULTI, .ctrl = 3, .seq = 3, .word = 3, .cycle = 962000});
	siols_advance(&B, 100000);
	assert(!siols_take_start(&B, true, &st));
	assert(siols_take_start(&B, false, &st) && st.seq == 3);
}

static void test_follower_ahead_keeps_anchor(void) {
	setup();
	siols_start(&B, false);
	siols_advance(&B, 100000);
	SioLsMsg st;
	inject(&B, (SioLsMsg){.type = SIOLS_START, .seq = 1, .cycle = 0});
	assert(siols_take_start(&B, false, &st));
	siols_reply(&B, &st, 1);
	assert(B.offset == 100000);
	siols_advance(&B, 40000); // 140000; next START maps to 130000: we are 10000 ahead
	inject(&B, (SioLsMsg){.type = SIOLS_START, .seq = 2, .cycle = 30000});
	assert(siols_take_start(&B, false, &st));
	assert(B.offset == 100000); // no ratchet forward
}

static void hook_sync_at(Fake* f) {
	if (f->polls == hook_at)
		inject(&B, (SioLsMsg){.type = SIOLS_SYNC, .cycle = SIOLS_SYNC_CYCLES});
}

static void test_follower_bound_wait_and_timeout(void) {
	setup();
	siols_start(&B, false);
	SioLsMsg st;
	inject(&B, (SioLsMsg){.type = SIOLS_START, .seq = 1, .cycle = 0});
	assert(siols_take_start(&B, false, &st));
	siols_reply(&B, &st, 1);
	siols_advance(&B, SIOLS_SLACK_CYCLES - 1);
	siols_service(&B, false);
	assert(B.st.bound_waits == 0);
	hook_at = fb.polls + 20;
	fb.hook = hook_sync_at;
	siols_advance(&B, 1); // at the bound
	siols_service(&B, false);
	assert(B.st.bound_waits == 1 && B.st.bound_timeouts == 0 && fb.us < SIOLS_BOUND_TIMEOUT_US);
	fb.hook = NULL;
	uint64_t t = fb.us;
	siols_advance(&B, SIOLS_SYNC_CYCLES); // at the new bound, leader silent
	siols_service(&B, false);
	assert(B.st.bound_timeouts == 1 && fb.us - t >= SIOLS_BOUND_TIMEOUT_US && !B.epoch_valid);
	assert(siols_next_event(&B) <= SIOLS_POLL_CYCLES); // running free
	// Re-anchored by the next SYNC; busy (mid-transfer) never waits.
	inject(&B, (SioLsMsg){.type = SIOLS_SYNC, .cycle = 200000});
	siols_advance(&B, SIOLS_SLACK_CYCLES);
	t = fb.us;
	siols_service(&B, true);
	assert(fb.us == t && B.st.bound_waits == 2); // 2nd = the wait that timed out above
}

static void hook_start_at(Fake* f) {
	if (f->polls == hook_at)
		inject(&B, (SioLsMsg){.type = SIOLS_START, .seq = 2, .cycle = 1000});
}

static void test_bound_wait_ends_on_start(void) {
	setup();
	siols_start(&B, false);
	SioLsMsg st;
	inject(&B, (SioLsMsg){.type = SIOLS_START, .seq = 1, .cycle = 0});
	assert(siols_take_start(&B, false, &st));
	siols_reply(&B, &st, 1);
	siols_advance(&B, SIOLS_SLACK_CYCLES);
	hook_at = fb.polls + 5;
	fb.hook = hook_start_at;
	siols_service(&B, false); // leader is stalled on us: its START must end the wait
	assert(B.st.bound_timeouts == 0 && fb.us < 1000);
	assert(siols_take_start(&B, false, &st) && st.seq == 2);
}

static void test_leader_sync_cadence(void) {
	setup();
	siols_start(&A, true);
	siols_advance(&A, SIOLS_FRAME_CYCLES);
	assert(fa.total_sent == 0); // idle link: silent
	siols_initiate(&A, SIOLS_MULTI, 3, 1);
	inject(&A, (SioLsMsg){.type = SIOLS_REPLY, .seq = 1, .word = 2});
	siols_complete(&A);
	fa.nout = 0;
	int syncs = 0;
	for (int i = 0; i < 281; i++)
		siols_advance(&A, 1000);
	for (int i = 0; i < fa.nout; i++) {
		SioLsMsg m;
		assert(siols_decode(fa.out[i], SIOLS_MSG_SIZE, &m) == 0);
		syncs += m.type == SIOLS_SYNC;
	}
	assert(syncs == 8);
	int n = fa.total_sent;
	siols_advance(&A, SIOLS_ENGAGE_CYCLES);
	siols_advance(&A, 2 * SIOLS_SYNC_CYCLES);
	assert(fa.total_sent == n); // link idle again: no SYNCs
}

static void test_next_event(void) {
	setup();
	assert(siols_next_event(&A) == UINT32_MAX);
	siols_start(&A, true);
	assert(siols_next_event(&A) == 1);
	siols_service(&A, false);
	assert(siols_next_event(&A) == SIOLS_IDLE_POLL_CYCLES);
	siols_initiate(&A, SIOLS_MULTI, 3, 1);
	inject(&A, (SioLsMsg){.type = SIOLS_REPLY, .seq = 1});
	siols_complete(&A);
	siols_advance(&A, 1);
	siols_service(&A, false);
	assert(siols_next_event(&A) == SIOLS_POLL_CYCLES);
}

// A = leader (client 0), B = follower; every leader poll runs a slice of B.
static void hook_run_follower(Fake* f) {
	(void)f;
	pump(&fa, &B);
	siols_advance(&B, 512);
	siols_service(&B, false);
	SioLsMsg st;
	while (siols_take_start(&B, false, &st))
		siols_reply(&B, &st, 0x2000 + st.seq);
	pump(&fb, &A);
}

static void test_two_instances_trade_burst(void) {
	setup();
	siols_start(&A, true);
	siols_start(&B, false);
	fa.hook = hook_run_follower;
	for (uint32_t i = 1; i <= 9; i++) { // one Gen3 trade frame: 9 transfers
		siols_advance(&A, 31000);
		siols_initiate(&A, SIOLS_MULTI, 3, 0x1000 + i);
		assert(siols_complete(&A) == 0x2000 + i);
	}
	assert(A.st.timeouts == 0 && B.st.bound_timeouts == 0 && A.st.xfers == 9);
	assert(A.st.wait_us_max < 1000); // follower lag absorbed by the min-gap rule
}

int main(void) {
	test_codec();
	test_unlinked_completes_at_once();
	test_leader_transfer();
	test_timeout_marks_broken();
	test_stop_mid_wait();
	test_stale_reply_ignored();
	test_follower_answers_and_dedupes();
	test_follower_late_answers_after_min_gap();
	test_follower_ahead_keeps_anchor();
	test_follower_bound_wait_and_timeout();
	test_bound_wait_ends_on_start();
	test_leader_sync_cadence();
	test_next_event();
	test_two_instances_trade_burst();
	printf("sio_lockstep tests: OK\n");
	return 0;
}
