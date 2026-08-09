/*
 * test_client.c — native protocol tests for m64p-server (Task 1: TCP layer).
 *
 * Spawns ./m64p-server-native on 127.0.0.1:55545 with --players 2, connects
 * real TCP sockets, and drives the wire formats from the Protocol Reference
 * (ground truth: mupen64plus-core/src/main/netplay.c). Seven numbered checks
 * T1-T7; any failure prints "FAIL <n> <name>" and exits 1. The spawned server
 * is killed in an atexit handler.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../server.h" /* rd32 / wr32 big-endian helpers */

#define TEST_PORT 55545
#define RECV_TIMEOUT_MS 3000

static pid_t server_pid = -1;
static int check_n = 0;

static void kill_server(void) {
	if (server_pid > 0) {
		kill(server_pid, SIGKILL);
		waitpid(server_pid, NULL, 0);
		server_pid = -1;
	}
}

#define CHECK(cond, name)                          \
	do {                                           \
		++check_n;                                 \
		if (cond) {                                \
			printf("ok %d %s\n", check_n, name);   \
		} else {                                   \
			printf("FAIL %d %s\n", check_n, name); \
			exit(1);                               \
		}                                          \
	} while (0)

static void spawn_server(void) {
	server_pid = fork();
	if (server_pid < 0) {
		perror("fork");
		exit(1);
	}
	if (server_pid == 0) {
		execl("./m64p-server-native", "m64p-server-native",
			  "--port", "55545", "--players", "2", "--buffer-target", "2",
			  (char*)NULL);
		perror("exec m64p-server-native");
		_exit(127);
	}
}

// Retry-connect until the freshly forked server is listening.
static int tcp_connect(void) {
	for (int i = 0; i < 50; ++i) {
		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0)
			return -1;
		struct sockaddr_in sa;
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_port = htons(TEST_PORT);
		sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0)
			return fd;
		close(fd);
		usleep(100 * 1000);
	}
	return -1;
}

static int send_all(int fd, const void* buf, size_t n) {
	const uint8_t* p = buf;
	while (n > 0) {
		ssize_t w = write(fd, p, n);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

// Read exactly n bytes, or fail after timeout_ms of total inactivity.
static int recv_all(int fd, void* buf, size_t n, int timeout_ms) {
	uint8_t* p = buf;
	while (n > 0) {
		struct pollfd pf = {.fd = fd, .events = POLLIN, .revents = 0};
		int pr = poll(&pf, 1, timeout_ms);
		if (pr <= 0)
			return -1; // timeout or error
		ssize_t r = read(fd, p, n);
		if (r <= 0) {
			if (r < 0 && errno == EINTR)
				continue;
			return -1; // peer closed or error
		}
		p += r;
		n -= (size_t)r;
	}
	return 0;
}

// 1 when the socket stays silent (no readable byte, no close) for ms.
static int silent_for(int fd, int ms) {
	struct pollfd pf = {.fd = fd, .events = POLLIN, .revents = 0};
	int pr = poll(&pf, 1, ms);
	if (pr == 0)
		return 1;
	fprintf(stderr, "  expected silence but socket became readable\n");
	return 0;
}

// [5][player][plugin][rawdata][u32 reg_id] -> 2-byte reply {ok, buffer_target}
static int do_register(int fd, uint8_t player, uint8_t plugin, uint8_t rawdata,
					   uint32_t reg_id, uint8_t out[2]) {
	uint8_t msg[8] = {5, player, plugin, rawdata, 0, 0, 0, 0};
	wr32(&msg[4], reg_id);
	if (send_all(fd, msg, sizeof(msg)))
		return -1;
	return recv_all(fd, out, 2, RECV_TIMEOUT_MS);
}

// One seat's 6 bytes of the GET_REGISTRATION table.
static int check_seat(const uint8_t* table, int seat, uint32_t reg_id,
					  uint8_t plugin, uint8_t rawdata) {
	const uint8_t* p = table + seat * 6;
	if (rd32(p) == reg_id && p[4] == plugin && p[5] == rawdata)
		return 1;
	fprintf(stderr, "  seat %d: got reg_id %08x plugin %u raw %u, want %08x/%u/%u\n",
			seat, rd32(p), p[4], p[5], reg_id, plugin, rawdata);
	return 0;
}

//////////////////////////////////
// Tests
//////////////////////////////////

static int sock_a = -1;
static int sock_b = -1;
static int udp_a = -1; // P1's UDP socket
static int udp_b = -1; // P2's UDP socket
static struct sockaddr_in server_udp_addr;

static int udp_socket(void) {
	return socket(AF_INET, SOCK_DGRAM, 0);
}

static int udp_send(int fd, const void* buf, size_t n) {
	ssize_t w = sendto(fd, buf, n, 0,
					   (struct sockaddr*)&server_udp_addr, sizeof(server_udp_addr));
	return (w == (ssize_t)n) ? 0 : -1;
}

// Returns bytes received, or -1 on timeout.
static int udp_recv(int fd, void* buf, size_t n, int timeout_ms) {
	struct pollfd pf = {.fd = fd, .events = POLLIN, .revents = 0};
	int pr = poll(&pf, 1, timeout_ms);
	if (pr <= 0)
		return -1;
	ssize_t r = recvfrom(fd, buf, n, 0, NULL, NULL);
	return (int)r;
}

/* T1  register P1: send [5][0][2][0][reg_id=0x1111] -> expect reply [1][2]
       (accepted, buffer_target 2)                                          */
static int t1(void) {
	uint8_t r[2];
	if (do_register(sock_a, 0, 2, 0, 0x1111, r)) {
		fprintf(stderr, "  no register reply\n");
		return 0;
	}
	if (r[0] != 1 || r[1] != 2) {
		fprintf(stderr, "  reply {%u,%u}, want {1,2}\n", r[0], r[1]);
		return 0;
	}
	return 1;
}

/* T2  conflict: second socket registers player 0 with reg_id 0x2222
       -> expect [0][2] (rejected)                                          */
static int t2(void) {
	uint8_t r[2];
	if (do_register(sock_b, 0, 0, 0, 0x2222, r)) {
		fprintf(stderr, "  no register reply\n");
		return 0;
	}
	if (r[0] != 0 || r[1] != 2) {
		fprintf(stderr, "  reply {%u,%u}, want {0,2}\n", r[0], r[1]);
		return 0;
	}
	return 1;
}

/* T3  start gate: socket A sends GET_REGISTRATION [6]; assert NO bytes
       arrive within 300 ms (poll with timeout); then socket B registers
       player 1 (plugin 2 -> expect demotion) with reg_id 0x2222 -> [1][2];
       now BOTH sockets receive 24 bytes: seat0 = {0x1111, plugin 2, raw 0},
       seat1 = {0x2222, plugin 1 (demoted from mempak), raw 0},
       seats 2-3 = zeroed                                                   */
static int t3(void) {
	uint8_t get_reg = 6;
	if (send_all(sock_a, &get_reg, 1))
		return 0;
	if (!silent_for(sock_a, 300))
		return 0;

	uint8_t r[2];
	if (do_register(sock_b, 1, 2, 0, 0x2222, r)) {
		fprintf(stderr, "  no register reply\n");
		return 0;
	}
	if (r[0] != 1 || r[1] != 2) {
		fprintf(stderr, "  reply {%u,%u}, want {1,2}\n", r[0], r[1]);
		return 0;
	}
	if (send_all(sock_b, &get_reg, 1))
		return 0;

	uint8_t table_a[24], table_b[24];
	if (recv_all(sock_a, table_a, 24, RECV_TIMEOUT_MS)) {
		fprintf(stderr, "  socket A never received the registration table\n");
		return 0;
	}
	if (recv_all(sock_b, table_b, 24, RECV_TIMEOUT_MS)) {
		fprintf(stderr, "  socket B never received the registration table\n");
		return 0;
	}
	const uint8_t* tables[2] = {table_a, table_b};
	for (int i = 0; i < 2; ++i) {
		if (!check_seat(tables[i], 0, 0x1111, 2, 0))
			return 0;
		if (!check_seat(tables[i], 1, 0x2222, 1, 0))
			return 0; // demoted from mempak
		if (!check_seat(tables[i], 2, 0, 0, 0))
			return 0;
		if (!check_seat(tables[i], 3, 0, 0, 0))
			return 0;
	}
	return 1;
}

/* T4  settings defer: socket B sends [4] first; assert no bytes in 300 ms;
       socket A sends [3] + 24 bytes 0x00..0x17; socket B then receives
       exactly those 24 bytes                                               */
static int t4(void) {
	uint8_t recv_settings = 4;
	if (send_all(sock_b, &recv_settings, 1))
		return 0;
	if (!silent_for(sock_b, 300))
		return 0;

	uint8_t msg[25];
	msg[0] = 3;
	for (int i = 0; i < 24; ++i)
		msg[1 + i] = (uint8_t)i;
	if (send_all(sock_a, msg, sizeof(msg)))
		return 0;

	uint8_t got[24];
	if (recv_all(sock_b, got, 24, RECV_TIMEOUT_MS)) {
		fprintf(stderr, "  socket B never received settings\n");
		return 0;
	}
	if (memcmp(got, &msg[1], 24) != 0) {
		fprintf(stderr, "  settings bytes differ\n");
		return 0;
	}
	return 1;
}

/* T5  save defer: socket B sends [2]"eep\0"; assert no bytes in 300 ms;
       socket A sends [1]"eep\0"[u32 512][512 bytes i&0xFF]; socket B
       receives exactly those 512 bytes                                     */
static int t5(void) {
	uint8_t req[5] = {2, 'e', 'e', 'p', 0};
	if (send_all(sock_b, req, sizeof(req)))
		return 0;
	if (!silent_for(sock_b, 300))
		return 0;

	uint8_t msg[1 + 4 + 4 + 512];
	msg[0] = 1;
	memcpy(&msg[1], "eep", 4);
	wr32(&msg[5], 512);
	for (int i = 0; i < 512; ++i)
		msg[9 + i] = (uint8_t)(i & 0xFF);
	if (send_all(sock_a, msg, sizeof(msg)))
		return 0;

	uint8_t got[512];
	if (recv_all(sock_b, got, 512, RECV_TIMEOUT_MS)) {
		fprintf(stderr, "  socket B never received the save blob\n");
		return 0;
	}
	if (memcmp(got, &msg[9], 512) != 0) {
		fprintf(stderr, "  save bytes differ\n");
		return 0;
	}
	return 1;
}

/* T6  second save ext works: A sends [1]"sra\0"[u32 32][32 x 0xAB];
       B sends [2]"sra\0" and receives them                                 */
static int t6(void) {
	uint8_t msg[1 + 4 + 4 + 32];
	msg[0] = 1;
	memcpy(&msg[1], "sra", 4);
	wr32(&msg[5], 32);
	memset(&msg[9], 0xAB, 32);
	if (send_all(sock_a, msg, sizeof(msg)))
		return 0;

	uint8_t req[5] = {2, 's', 'r', 'a', 0};
	if (send_all(sock_b, req, sizeof(req)))
		return 0;

	uint8_t got[32];
	if (recv_all(sock_b, got, 32, RECV_TIMEOUT_MS)) {
		fprintf(stderr, "  socket B never received the second save blob\n");
		return 0;
	}
	for (int i = 0; i < 32; ++i) {
		if (got[i] != 0xAB) {
			fprintf(stderr, "  byte %d is %02x, want ab\n", i, got[i]);
			return 0;
		}
	}
	return 1;
}

/* T7  disconnect notice: socket B sends [7][0x2222]; then poke the UDP
       side (Task 2 asserts the status bit; in this task just assert the
       server stays up and seat 1 can be re-registered with reg_id 0x3333
       -> reply [1][2])                                                     */
static int t7(void) {
	uint8_t msg[5] = {7, 0, 0, 0, 0};
	wr32(&msg[1], 0x2222);
	if (send_all(sock_b, msg, sizeof(msg)))
		return 0;

	uint8_t r[2];
	if (do_register(sock_b, 1, 0, 0, 0x3333, r)) {
		fprintf(stderr, "  no register reply after disconnect notice\n");
		return 0;
	}
	if (r[0] != 1 || r[1] != 2) {
		fprintf(stderr, "  reply {%u,%u}, want {1,2}\n", r[0], r[1]);
		return 0;
	}
	return 1;
}

/* T8  input round-trip: send type-0 [0][0][count=0][keys=0xAABBCCDD][plugin=2]
       from P1's UDP socket; the server materialises the input and pushes a
       GRATUITOUS (type 3) packet back to that same socket (its addr was just
       learned): assert data[0]==3, data[1]==0, data[4]>=1, first event
       count==0, keys==0xAABBCCDD.                                            */
static int t8(void) {
	uint8_t pkt[11];
	pkt[0] = 0;
	pkt[1] = 0;
	wr32(&pkt[2], 0);		   // count
	wr32(&pkt[6], 0xAABBCCDD); // keys
	pkt[10] = 2;			   // plugin
	if (udp_send(udp_a, pkt, sizeof(pkt)))
		return 0;

	uint8_t r[512];
	int n = udp_recv(udp_a, r, sizeof(r), RECV_TIMEOUT_MS);
	if (n < 14) {
		fprintf(stderr, "  no gratuitous packet (n=%d)\n", n);
		return 0;
	}
	if (r[0] != 3 || r[1] != 0) {
		fprintf(stderr, "  type/player %u/%u, want 3/0\n", r[0], r[1]);
		return 0;
	}
	if (r[4] < 1) {
		fprintf(stderr, "  no events in gratuitous packet\n");
		return 0;
	}
	if (rd32(&r[5]) != 0) {
		fprintf(stderr, "  first event count %u, want 0\n", rd32(&r[5]));
		return 0;
	}
	if (rd32(&r[9]) != 0xAABBCCDD) {
		fprintf(stderr, "  first event keys %08x, want aabbccdd\n", rd32(&r[9]));
		return 0;
	}
	return 1;
}

/* T9  request: send type-2 [2][0][reg=0x1111][count=0][spec=0][bufsz=0]
       from P1's socket; assert a type-1 reply: data[0]==1, data[1]==0,
       count 0 event has keys 0xAABBCCDD, count_lag byte == 0.               */
static int t9(void) {
	uint8_t pkt[12];
	pkt[0] = 2;
	pkt[1] = 0;
	wr32(&pkt[2], 0x1111); // reg_id
	wr32(&pkt[6], 0);	   // count
	pkt[10] = 0;		   // spectator
	pkt[11] = 0;		   // local buffer size
	if (udp_send(udp_a, pkt, sizeof(pkt)))
		return 0;

	uint8_t r[512];
	int n = udp_recv(udp_a, r, sizeof(r), RECV_TIMEOUT_MS);
	if (n < 14) {
		fprintf(stderr, "  no reply (n=%d)\n", n);
		return 0;
	}
	if (r[0] != 1 || r[1] != 0) {
		fprintf(stderr, "  type/player %u/%u, want 1/0\n", r[0], r[1]);
		return 0;
	}
	if (r[3] != 0) {
		fprintf(stderr, "  count_lag %u, want 0\n", r[3]);
		return 0;
	}
	if (rd32(&r[5]) != 0 || rd32(&r[9]) != 0xAABBCCDD) {
		fprintf(stderr, "  event0 count/keys %u/%08x, want 0/aabbccdd\n",
				rd32(&r[5]), rd32(&r[9]));
		return 0;
	}
	return 1;
}

/* T10 fabrication: send type-2 for player 0, count=1 (no type-0 sent for
       counts 1+): reply must contain fabricated events for counts >= 1 with
       keys 0xAABBCCDD (copied from pending input), up to buffer_size ahead. */
static int t10(void) {
	uint8_t pkt[12];
	pkt[0] = 2;
	pkt[1] = 0;
	wr32(&pkt[2], 0x1111);
	wr32(&pkt[6], 1); // count
	pkt[10] = 0;
	pkt[11] = 0;
	if (udp_send(udp_a, pkt, sizeof(pkt)))
		return 0;

	uint8_t r[512];
	int n = udp_recv(udp_a, r, sizeof(r), RECV_TIMEOUT_MS);
	if (n < 14) {
		fprintf(stderr, "  no reply (n=%d)\n", n);
		return 0;
	}
	if (r[0] != 1 || r[4] < 1) {
		fprintf(stderr, "  type %u n_events %u\n", r[0], r[4]);
		return 0;
	}
	if (rd32(&r[5]) != 1) {
		fprintf(stderr, "  first fabricated count %u, want 1\n", rd32(&r[5]));
		return 0;
	}
	if (rd32(&r[9]) != 0xAABBCCDD) {
		fprintf(stderr, "  fabricated keys %08x, want aabbccdd\n", rd32(&r[9]));
		return 0;
	}
	return 1;
}

/* T11 lag: P1 requests count=10 first (lead becomes 10), then P2 requests
       count=2 for player 0 with reg=0x3333 -> count_lag byte == 8.          */
static int t11(void) {
	uint8_t r[512];

	uint8_t p1[12];
	p1[0] = 2;
	p1[1] = 0;
	wr32(&p1[2], 0x1111);
	wr32(&p1[6], 10); // count -> lead becomes 10
	p1[10] = 0;
	p1[11] = 0;
	if (udp_send(udp_a, p1, sizeof(p1)))
		return 0;
	udp_recv(udp_a, r, sizeof(r), RECV_TIMEOUT_MS); // drain P1's reply

	uint8_t p2[12];
	p2[0] = 2;
	p2[1] = 0;			  // requesting player 0's input
	wr32(&p2[2], 0x3333); // sender is P2 (seat 1)
	wr32(&p2[6], 2);	  // count
	p2[10] = 0;
	p2[11] = 0;
	if (udp_send(udp_b, p2, sizeof(p2)))
		return 0;

	int n = udp_recv(udp_b, r, sizeof(r), RECV_TIMEOUT_MS);
	if (n < 5) {
		fprintf(stderr, "  no reply (n=%d)\n", n);
		return 0;
	}
	if (r[0] != 1)
		return 0;
	if (r[3] != 8) {
		fprintf(stderr, "  count_lag %u, want 8\n", r[3]);
		return 0;
	}
	return 1;
}

/* T12 desync: send type-4 [4][vi=600][128 bytes 0x00] from P1's socket and
       [4][vi=600][128 bytes 0xFF] from P2's socket; next type-2 reply has
       (data[2] & 0x1) == 1.                                                 */
static int t12(void) {
	uint8_t s0[133];
	s0[0] = 4;
	wr32(&s0[1], 600);
	memset(&s0[5], 0x00, 128);
	if (udp_send(udp_a, s0, sizeof(s0)))
		return 0;

	uint8_t sf[133];
	sf[0] = 4;
	wr32(&sf[1], 600);
	memset(&sf[5], 0xFF, 128);
	if (udp_send(udp_b, sf, sizeof(sf)))
		return 0;

	usleep(100 * 1000); // let the server process both sync packets

	uint8_t pkt[12];
	pkt[0] = 2;
	pkt[1] = 0;
	wr32(&pkt[2], 0x1111);
	wr32(&pkt[6], 0);
	pkt[10] = 0;
	pkt[11] = 0;
	if (udp_send(udp_a, pkt, sizeof(pkt)))
		return 0;

	uint8_t r[512];
	int n = udp_recv(udp_a, r, sizeof(r), RECV_TIMEOUT_MS);
	if (n < 5) {
		fprintf(stderr, "  no reply (n=%d)\n", n);
		return 0;
	}
	if ((r[2] & 0x1) != 1) {
		fprintf(stderr, "  status %02x, desync bit unset\n", r[2]);
		return 0;
	}
	return 1;
}

/* T13 disconnect status: send TCP [7][0x3333] on socket B; next type-1
       reply on P1's socket has (data[2] & (1<<2)) != 0 (player 2 gone).     */
static int t13(void) {
	uint8_t msg[5] = {7, 0, 0, 0, 0};
	wr32(&msg[1], 0x3333);
	if (send_all(sock_b, msg, sizeof(msg)))
		return 0;

	usleep(100 * 1000); // let the server process the disconnect notice

	uint8_t pkt[12];
	pkt[0] = 2;
	pkt[1] = 0;
	wr32(&pkt[2], 0x1111);
	wr32(&pkt[6], 0);
	pkt[10] = 0;
	pkt[11] = 0;
	if (udp_send(udp_a, pkt, sizeof(pkt)))
		return 0;

	uint8_t r[512];
	int n = udp_recv(udp_a, r, sizeof(r), RECV_TIMEOUT_MS);
	if (n < 5) {
		fprintf(stderr, "  no reply (n=%d)\n", n);
		return 0;
	}
	if ((r[2] & (1 << 2)) == 0) {
		fprintf(stderr, "  status %02x, player-2 disconnect bit unset\n", r[2]);
		return 0;
	}
	return 1;
}

int main(void) {
	signal(SIGPIPE, SIG_IGN);
	atexit(kill_server);
	spawn_server();

	sock_a = tcp_connect();
	if (sock_a < 0) {
		fprintf(stderr, "could not connect socket A to 127.0.0.1:%d\n", TEST_PORT);
		exit(1);
	}
	sock_b = tcp_connect();
	if (sock_b < 0) {
		fprintf(stderr, "could not connect socket B to 127.0.0.1:%d\n", TEST_PORT);
		exit(1);
	}

	memset(&server_udp_addr, 0, sizeof(server_udp_addr));
	server_udp_addr.sin_family = AF_INET;
	server_udp_addr.sin_port = htons(TEST_PORT);
	server_udp_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	udp_a = udp_socket();
	udp_b = udp_socket();
	if (udp_a < 0 || udp_b < 0) {
		fprintf(stderr, "could not open UDP sockets\n");
		exit(1);
	}

	CHECK(t1(), "T1 register P1 accepted with buffer_target");
	CHECK(t2(), "T2 register conflict rejected");
	CHECK(t3(), "T3 GET_REGISTRATION gated on full lobby, mempak demoted");
	CHECK(t4(), "T4 RECEIVE_SETTINGS deferred until P1 sends settings");
	CHECK(t5(), "T5 RECEIVE_SAVE deferred until P1 sends the save");
	CHECK(t6(), "T6 second save extension stored and served");
	CHECK(t7(), "T7 disconnect notice frees the seat for re-registration");
	CHECK(t8(), "T8 type-0 input round-trip, gratuitous type-3 push");
	CHECK(t9(), "T9 type-2 request returns stored input, count_lag 0");
	CHECK(t10(), "T10 fabrication ahead of stored input from pending");
	CHECK(t11(), "T11 count_lag reflects distance behind lead");
	CHECK(t12(), "T12 desync detection sets status bit 0");
	CHECK(t13(), "T13 disconnect notice sets player-2 status bit");

	return 0;
}
