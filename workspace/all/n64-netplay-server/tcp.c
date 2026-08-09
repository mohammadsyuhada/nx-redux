/*
 * tcp.c — the netplay server's TCP request layer.
 *
 * ms_tcp_handle_conn is a resumable parser over c->buf: bytes accumulate
 * there across reads, and a request is consumed only once the whole unit is
 * available, so a request split across TCP segments simply waits and several
 * requests batched into one read all get handled in one call. Requests whose
 * reply data may not exist yet (RECEIVE_SETTINGS / RECEIVE_SAVE / the
 * GET_REGISTRATION start gate) queue a deferred reply; the queue is FIFO per
 * connection so replies on one socket stay in request order, and
 * ms_tcp_service_deferred fires each entry once its data condition is met
 * (or drops the connection after MS_DEFER_TIMEOUT_MS).
 *
 * Wire formats: mupen64plus-core/src/main/netplay.c, big-endian throughout.
 */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "server.h"

void ms_conn_close(MsServer* s, MsConn* c) {
	(void)s;
	if (c->fd >= 0)
		close(c->fd);
	free(c->buf);
	memset(c, 0, sizeof(*c));
	c->fd = -1;
	c->request = MS_TCP_NONE;
}

/*
 * Write the whole reply now. The fd is nonblocking, so poll briefly for
 * writability on a full send buffer; replies here are tiny (2-24 bytes) or a
 * save blob a client is actively waiting to read, so this cannot stall the
 * loop in practice. Returns 0, or -1 when the peer is gone.
 */
static int write_full(int fd, const void* buf, size_t n) {
	const uint8_t* p = buf;
	while (n > 0) {
		ssize_t w = write(fd, p, n);
		if (w > 0) {
			p += w;
			n -= (size_t)w;
			continue;
		}
		if (w < 0 && errno == EINTR)
			continue;
		if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			struct pollfd pf = {.fd = fd, .events = POLLOUT, .revents = 0};
			if (poll(&pf, 1, 1000) <= 0)
				return -1;
			continue;
		}
		return -1;
	}
	return 0;
}

static void consume(MsConn* c, size_t n) {
	c->len -= n;
	memmove(c->buf, c->buf + n, c->len);
}

/*
 * The NUL-terminated save-extension string at the head of c->buf.
 * Returns 1 with ext + its wire length (incl. NUL) filled in, 0 when more
 * bytes are needed, -1 after closing the conn on a malformed extension.
 */
static int parse_ext(MsServer* s, MsConn* c, char ext[16], size_t* wire_len) {
	size_t limit = c->len < 16 ? c->len : 16;
	const uint8_t* nul = memchr(c->buf, 0, limit);
	if (nul == NULL) {
		if (c->len >= 16) {
			ms_log("save extension longer than 15 bytes, closing conn");
			ms_conn_close(s, c);
			return -1;
		}
		return 0;
	}
	size_t n = (size_t)(nul - c->buf);
	memcpy(ext, c->buf, n);
	ext[n] = '\0';
	*wire_len = n + 1;
	return 1;
}

// Store/overwrite the blob for ext. Size 0 stores an empty valid blob (the
// client sends all-zero data for "no save", but guard anyway).
static void save_store(MsServer* s, const char* ext, const uint8_t* data, uint32_t size) {
	MsSave* slot = NULL;
	for (int i = 0; i < MS_MAX_SAVES; ++i) {
		if (s->saves[i].valid && strcmp(s->saves[i].ext, ext) == 0) {
			slot = &s->saves[i];
			break;
		}
	}
	if (slot == NULL) {
		for (int i = 0; i < MS_MAX_SAVES; ++i) {
			if (!s->saves[i].valid) {
				slot = &s->saves[i];
				break;
			}
		}
	}
	if (slot == NULL) {
		ms_log("save table full (%d extensions), dropping .%s", MS_MAX_SAVES, ext);
		return;
	}
	uint8_t* copy = malloc(size ? size : 1);
	if (copy == NULL) {
		ms_log("out of memory storing save .%s (%u bytes)", ext, size);
		return;
	}
	memcpy(copy, data, size);
	free(slot->data);
	slot->data = copy;
	slot->size = size;
	slot->valid = 1;
	snprintf(slot->ext, sizeof(slot->ext), "%s", ext);
	ms_log("stored save .%s (%u bytes)", ext, size);
}

// Queue a deferred reply on c. Returns 0, or -1 after closing an
// overflowing conn (8 unanswered blocking requests means a broken client).
static int defer_push(MsServer* s, MsConn* c, MsDeferKind kind, const char* ext) {
	if (c->defer_n >= (int)(sizeof(c->defer) / sizeof(c->defer[0]))) {
		ms_log("deferred-reply queue full, closing conn");
		ms_conn_close(s, c);
		return -1;
	}
	c->defer[c->defer_n].kind = kind;
	c->defer[c->defer_n].since_ms = ms_now_ms();
	if (ext != NULL)
		snprintf(c->defer[c->defer_n].ext, sizeof(c->defer[c->defer_n].ext), "%s", ext);
	else
		c->defer[c->defer_n].ext[0] = '\0';
	++c->defer_n;
	return 0;
}

void ms_tcp_handle_conn(MsServer* s, MsConn* c) {
	for (;;) {
		if (c->request == MS_TCP_NONE) {
			if (c->len < 1)
				return;
			c->request = c->buf[0];
			consume(c, 1);
		}

		switch (c->request) {
		case MS_TCP_REGISTER: { // [u8 player][u8 plugin][u8 rawdata][u32 reg_id]
			if (c->len < 7)
				return;
			uint8_t player = c->buf[0];
			uint8_t plugin = c->buf[1];
			uint8_t rawdata = c->buf[2];
			uint32_t reg_id = rd32(&c->buf[3]);
			consume(c, 7);

			uint8_t reply[2] = {0, (uint8_t)s->buffer_target};
			if (player < MS_MAX_PLAYERS) {
				MsPlayer* p = &s->players[player];
				if (p->reg_id != 0) {
					// Same reg_id re-registering the same seat is fine.
					reply[0] = (p->reg_id == reg_id) ? 1 : 0;
				} else {
					if (player > 0 && plugin == 2)
						plugin = 1; // mempak: P1 only
					memset(p, 0, sizeof(*p));
					p->reg_id = reg_id;
					p->plugin = plugin;
					p->rawdata = rawdata;
					p->alive = 1;
					p->pending_keys = 0;
					p->pending_plugin = plugin;
					// A seat re-registered after a DISCONNECT_NOTICE must no
					// longer read as disconnected: clear its status bit, which
					// UDP now broadcasts in every type-1/3 packet.
					s->status &= (uint8_t)~(1u << (player + 1));
					s->ever_registered = 1;
					ms_log("registered player %u reg_id %08x", player + 1, reg_id);
					reply[0] = 1;
				}
			}
			if (write_full(c->fd, reply, 2)) {
				ms_conn_close(s, c);
				return;
			}
			c->request = MS_TCP_NONE;
			continue;
		}

		case MS_TCP_GET_REG: // reply gated on a full lobby
			if (defer_push(s, c, MS_DEFER_REG, NULL))
				return;
			c->request = MS_TCP_NONE;
			continue;

		case MS_TCP_SEND_SETTINGS: { // [24 settings bytes], P1 only
			if (c->len < MS_SETTINGS_SIZE)
				return;
			memcpy(s->settings, c->buf, MS_SETTINGS_SIZE);
			s->has_settings = 1;
			consume(c, MS_SETTINGS_SIZE);
			ms_log("received settings");
			c->request = MS_TCP_NONE;
			continue;
		}

		case MS_TCP_RECV_SETTINGS: // reply gated on P1's settings
			if (defer_push(s, c, MS_DEFER_SETTINGS, NULL))
				return;
			c->request = MS_TCP_NONE;
			continue;

		case MS_TCP_SEND_SAVE: { // [ext\0][u32 size][size bytes], P1 only
			char ext[16];
			size_t ext_len;
			int r = parse_ext(s, c, ext, &ext_len);
			if (r <= 0)
				return; // need more bytes, or conn closed
			if (c->len < ext_len + 4)
				return;
			uint32_t size = rd32(&c->buf[ext_len]);
			if (size > MS_SAVE_MAX) {
				ms_log("save .%s claims %u bytes (cap %u), closing conn", ext, size, MS_SAVE_MAX);
				ms_conn_close(s, c);
				return;
			}
			if (c->len < ext_len + 4 + size)
				return;
			save_store(s, ext, &c->buf[ext_len + 4], size);
			consume(c, ext_len + 4 + size);
			c->request = MS_TCP_NONE;
			continue;
		}

		case MS_TCP_RECV_SAVE: { // [ext\0], reply gated on P1's blob
			char ext[16];
			size_t ext_len;
			int r = parse_ext(s, c, ext, &ext_len);
			if (r <= 0)
				return;
			consume(c, ext_len);
			if (defer_push(s, c, MS_DEFER_SAVE, ext))
				return;
			c->request = MS_TCP_NONE;
			continue;
		}

		case MS_TCP_DISCONNECT: { // [u32 reg_id], no reply
			if (c->len < 4)
				return;
			uint32_t reg_id = rd32(c->buf);
			consume(c, 4);
			for (int i = 0; i < MS_MAX_PLAYERS; ++i) {
				MsPlayer* p = &s->players[i];
				if (reg_id != 0 && p->reg_id == reg_id) {
					p->reg_id = 0;
					p->addr_valid = 0;
					p->alive = 0;
					s->status |= (uint8_t)(1u << (i + 1));
					ms_log("player %u disconnected (notice)", i + 1);
				}
			}
			c->request = MS_TCP_NONE;
			continue;
		}

		default:
			ms_log("unknown TCP request %u, closing", c->request);
			ms_conn_close(s, c);
			return;
		}
	}
}

void ms_tcp_service_deferred(MsServer* s) {
	long long now = ms_now_ms();
	for (int ci = 0; ci < MS_MAX_CONNS; ++ci) {
		MsConn* c = &s->conns[ci];
		while (c->fd >= 0 && c->defer_n > 0) {
			uint8_t table[MS_MAX_PLAYERS * 6];
			const uint8_t* payload = NULL;
			size_t payload_n = 0;
			int ready = 0;

			switch (c->defer[0].kind) {
			case MS_DEFER_REG: // fires once the lobby is full (start gate)
				if (ms_registered_count(s) == s->expected_players) {
					memset(table, 0, sizeof(table)); // reg_id 0 = seat empty
					for (int i = 0; i < MS_MAX_PLAYERS; ++i) {
						const MsPlayer* p = &s->players[i];
						if (p->reg_id == 0)
							continue;
						wr32(&table[i * 6], p->reg_id);
						table[i * 6 + 4] = p->plugin;
						table[i * 6 + 5] = p->rawdata;
					}
					payload = table;
					payload_n = sizeof(table);
					ready = 1;
				}
				break;
			case MS_DEFER_SETTINGS:
				if (s->has_settings) {
					payload = s->settings;
					payload_n = MS_SETTINGS_SIZE;
					ready = 1;
				}
				break;
			case MS_DEFER_SAVE:
				for (int i = 0; i < MS_MAX_SAVES; ++i) {
					if (s->saves[i].valid && strcmp(s->saves[i].ext, c->defer[0].ext) == 0) {
						payload = s->saves[i].data;
						payload_n = s->saves[i].size;
						ready = 1;
						break;
					}
				}
				break;
			default: // MS_DEFER_NONE: nothing to send, just drop the entry
				ready = 1;
				break;
			}

			if (!ready) {
				if (now - c->defer[0].since_ms > MS_DEFER_TIMEOUT_MS) {
					ms_log("deferred reply unmet after %d ms, closing conn", MS_DEFER_TIMEOUT_MS);
					ms_conn_close(s, c);
				}
				// Replies on one socket must stay ordered: stop at the
				// first unmet defer rather than scanning past it.
				break;
			}
			if (payload_n > 0 && write_full(c->fd, payload, payload_n)) {
				ms_conn_close(s, c);
				break;
			}
			--c->defer_n;
			memmove(&c->defer[0], &c->defer[1], (size_t)c->defer_n * sizeof(c->defer[0]));
		}
	}
}
