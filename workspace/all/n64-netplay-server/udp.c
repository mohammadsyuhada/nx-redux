/*
 * udp.c — the netplay server's UDP input layer.
 *
 * This is the piece that makes UDP loss harmless: the server, not the client,
 * is the authority for which keys apply to which count. Each seat keeps a
 * "pending" input (the last keys+plugin it reported), a ring of stored inputs
 * keyed by count, and its UDP address. When asked for a count it doesn't have,
 * the server fabricates one by copying the pending input — a dropped or
 * reordered packet just gets regenerated deterministically.
 *
 * Wire formats: mupen64plus-core/src/main/netplay.c, big-endian throughout.
 * The client parses type-1/3 replies at netplay.c:289-334 (5-byte header then
 * n x [u32 count][u32 keys][u8 plugin]); a datagram never exceeds 508 bytes.
 *
 * Sends are single sendto() calls: unlike the tiny TCP replies there is no
 * write_full()-style retry loop, so a slow send can never park the poll loop.
 */

#include <string.h>
#include <sys/socket.h>

#include "server.h"

#define MS_UDP_PKT_MAX 508 /* largest type-1/3 datagram */
#define MS_EVENT_SIZE 9	   /* [u32 count][u32 keys][u8 plugin] */
#define MS_KEY_HDR 5	   /* [type][player][status][count_lag][n] */

//////////////////////////////////
// Per-seat input store
//////////////////////////////////

static MsInput* slot(MsPlayer* p, uint32_t count) {
	return &p->inputs[count % MS_INPUT_RING];
}

// Do we already hold this exact count? (The ring index aliases, so the stored
// count must match — a stale entry from ~68 s ago reads as "not held".)
static int have(MsPlayer* p, uint32_t count) {
	MsInput* in = slot(p, count);
	return in->valid && in->count == count;
}

// Return the stored input for count, fabricating from pending if absent.
static MsInput fill_input(MsPlayer* p, uint32_t count) {
	MsInput* in = slot(p, count);
	if (!(in->valid && in->count == count)) {
		in->count = count;
		in->keys = p->pending_keys;
		in->plugin = p->pending_plugin;
		in->valid = 1;
	}
	return *in;
}

static int seat_by_regid(MsServer* s, uint32_t reg_id) {
	if (reg_id == 0)
		return -1;
	for (int i = 0; i < MS_MAX_PLAYERS; ++i) {
		if (s->players[i].reg_id == reg_id)
			return i;
	}
	return -1;
}

/*
 * Build one type-1/type-3 reply for `player` starting at `count` into out.
 * Emits events while packet space remains (stop once fewer than 9 bytes are
 * left) AND either the store already holds the count (always relay what
 * exists) OR the requester is a caught-up non-spectator, in which case we
 * generate up to buffer_size events ahead of the request for the lead player.
 * Returns the packet length, or 0 when no event was emitted (send nothing).
 */
static size_t build_key_packet(MsServer* s, uint8_t* out, uint8_t type,
							   uint8_t player, uint32_t start,
							   int spectator, uint32_t count_lag) {
	MsPlayer* p = &s->players[player];
	out[0] = type;
	out[1] = player;
	out[2] = s->status;
	out[3] = (uint8_t)count_lag;

	uint8_t n = 0;
	uint32_t count = start;
	uint32_t end = start + s->buffer_size;
	while (MS_KEY_HDR + ((size_t)n + 1) * MS_EVENT_SIZE <= MS_UDP_PKT_MAX &&
		   (have(p, count) ||
			(!spectator && count_lag == 0 && ms_uint_larger(end, count)))) {
		MsInput in = fill_input(p, count);
		uint8_t* ev = out + MS_KEY_HDR + (size_t)n * MS_EVENT_SIZE;
		wr32(ev, count);
		wr32(ev + 4, in.keys);
		ev[8] = in.plugin;
		++n;
		++count;
	}
	out[4] = n;
	return n ? MS_KEY_HDR + (size_t)n * MS_EVENT_SIZE : 0;
}

//////////////////////////////////
// Packet dispatch
//////////////////////////////////

void ms_udp_handle_packet(MsServer* s, const uint8_t* pkt, size_t len,
						  const struct sockaddr_in* from) {
	if (len < 1)
		return;
	uint8_t out[MS_UDP_PKT_MAX];

	switch (pkt[0]) {
	case MS_UDP_SEND_KEY: { // [0][u8 player][u32 count][u32 keys][u8 plugin]
		if (len < 11)
			return;
		uint8_t player = pkt[1];
		if (player >= MS_MAX_PLAYERS)
			return;
		MsPlayer* p = &s->players[player];
		p->addr = *from;
		p->addr_valid = 1;
		p->pending_keys = rd32(pkt + 6);
		p->pending_plugin = pkt[10];
		uint32_t count = rd32(pkt + 2);
		// Materialise the client's reported input for this count so the
		// gratuitous push below (a spectator-style send that never
		// fabricates) actually carries it — the client relies on the echo.
		fill_input(p, count);
		// Push a gratuitous (type 3) update for this seat to every peer whose
		// address we know; the client ignores the count_lag byte on type 3.
		for (int i = 0; i < MS_MAX_PLAYERS; ++i) {
			if (!s->players[i].addr_valid)
				continue;
			size_t n = build_key_packet(s, out, MS_UDP_RECV_KEY_GRATUITOUS,
										player, count, 1, 0);
			if (n)
				sendto(s->udp_fd, out, n, 0,
					   (struct sockaddr*)&s->players[i].addr,
					   sizeof(s->players[i].addr));
		}
		break;
	}

	case MS_UDP_REQUEST_KEY: { // [2][player][u32 reg][u32 count][u8 spec][u8 bufsz]
		if (len < 12)
			return;
		uint8_t player = pkt[1];
		if (player >= MS_MAX_PLAYERS)
			return;
		uint32_t reg = rd32(pkt + 2);
		uint32_t count = rd32(pkt + 6);
		int spec = pkt[10] != 0;
		uint8_t bufsz = pkt[11];
		int sender = seat_by_regid(s, reg);
		if (sender < 0) {
			static int warned = 0;
			if (!warned) {
				ms_log("UDP request from unknown reg_id %08x, ignoring", reg);
				warned = 1;
			}
			return;
		}
		if (!spec && ms_uint_larger(count, s->lead_count))
			s->lead_count = count;
		uint32_t lag = ms_uint_larger(count, s->lead_count) ? 0
															: s->lead_count - count;
		size_t n = build_key_packet(s, out, MS_UDP_RECV_KEY, player, count,
									spec, lag);
		if (n)
			sendto(s->udp_fd, out, n, 0, (struct sockaddr*)from, sizeof(*from));
		// Buffer-management samples + liveness are keyed on the sender, not
		// the requested player.
		MsPlayer* sp = &s->players[sender];
		sp->alive = 1;
		sp->lag_sum += lag;
		sp->health_sum += bufsz;
		sp->sample_n += 1;
		break;
	}

	case MS_UDP_SYNC: { // [4][u32 vi][128-byte CP0 blob]
		if (len < 133)
			return;
		if (s->status & 0x1)
			return; // already desynced; nothing to learn
		uint32_t vi = rd32(pkt + 1);
		MsSync* y = &s->syncs[vi % MS_SYNC_RING];
		if (!y->valid || y->vi != vi) {
			y->vi = vi;
			y->valid = 1;
			memcpy(y->blob, pkt + 5, MS_CP0_BLOB);
		} else if (memcmp(y->blob, pkt + 5, MS_CP0_BLOB) != 0) {
			s->status |= 0x1;
			ms_log("DESYNC at VI %u", vi);
		}
		break;
	}

	default:
		break; // unknown UDP type: ignore (loss-tolerant protocol)
	}
}

//////////////////////////////////
// Periodic ticks
//////////////////////////////////

/*
 * 1 s: nudge the server-side lookahead window toward buffer_target. Average
 * each seat's count_lag and reported local buffer health over the last second;
 * the seat with the lowest average lag is the lead. If the lead is sitting on
 * more buffer than we want, shrink the window; if it is starving, grow it. A
 * 0.75 dead-band keeps it from oscillating.
 */
void ms_tick_buffer(MsServer* s) {
	int lead = -1;
	double lead_lag = 0.0, lead_health = 0.0;
	for (int i = 0; i < MS_MAX_PLAYERS; ++i) {
		MsPlayer* p = &s->players[i];
		if (p->sample_n == 0)
			continue;
		double avg_lag = p->lag_sum / p->sample_n;
		double avg_health = p->health_sum / p->sample_n;
		if (lead < 0 || avg_lag < lead_lag) {
			lead = i;
			lead_lag = avg_lag;
			lead_health = avg_health;
		}
	}

	if (lead >= 0) {
		uint32_t before = s->buffer_size;
		if (lead_health > s->buffer_target + 0.75 && s->buffer_size > 0)
			--s->buffer_size;
		else if (lead_health < s->buffer_target - 0.75)
			++s->buffer_size;
		if (s->buffer_size != before)
			ms_log("buffer_size %u -> %u (lead seat %d, health %.2f)",
				   before, s->buffer_size, lead + 1, lead_health);
	}

	for (int i = 0; i < MS_MAX_PLAYERS; ++i) {
		s->players[i].lag_sum = 0.0;
		s->players[i].health_sum = 0.0;
		s->players[i].sample_n = 0;
	}
}

/*
 * 30 s: any registered seat that made no input request since the previous
 * sweep is gone — flag it disconnected and free it. Runs only once at least
 * one player has ever registered; returns 1 (server should exit) once every
 * registered seat is gone.
 */
int ms_tick_sweep(MsServer* s) {
	if (!s->ever_registered)
		return 0;

	for (int i = 0; i < MS_MAX_PLAYERS; ++i) {
		MsPlayer* p = &s->players[i];
		if (p->reg_id == 0)
			continue;
		if (!p->alive) {
			s->status |= (uint8_t)(1u << (i + 1));
			ms_log("player %u timed out (no input request in 30 s)", i + 1);
			memset(p, 0, sizeof(*p)); // reg_id 0 = seat free
		}
	}
	for (int i = 0; i < MS_MAX_PLAYERS; ++i)
		s->players[i].alive = 0;

	return ms_registered_count(s) == 0;
}
