#ifndef MS_SERVER_H
#define MS_SERVER_H

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>

/* Big-endian 32-bit read/write (wire order, mupen64plus netplay protocol). */
static inline uint32_t rd32(const uint8_t* p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void wr32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

#define MS_MAX_PLAYERS 4
#define MS_MAX_CONNS 16
#define MS_INPUT_RING 4096 /* per-seat input events kept (~68 s at 60 Hz) */
#define MS_SYNC_RING 128   /* CP0 sync snapshots kept */
#define MS_SETTINGS_SIZE 24
#define MS_MAX_SAVES 8		   /* distinct save-file extensions per session */
#define MS_SAVE_MAX (4u << 20) /* sanity cap on a single save blob */
#define MS_CONN_BUF_MAX (MS_SAVE_MAX + 64)
#define MS_DEFER_TIMEOUT_MS (5 * 60 * 1000)
#define MS_CP0_BLOB 128 /* 32 regs x 4 bytes */

/* TCP request types (wire values, netplay.c:68-74) */
enum { MS_TCP_SEND_SAVE = 1,
	   MS_TCP_RECV_SAVE = 2,
	   MS_TCP_SEND_SETTINGS = 3,
	   MS_TCP_RECV_SETTINGS = 4,
	   MS_TCP_REGISTER = 5,
	   MS_TCP_GET_REG = 6,
	   MS_TCP_DISCONNECT = 7,
	   MS_TCP_NONE = 255 };
/* UDP types (netplay.c:61-65) */
enum { MS_UDP_SEND_KEY = 0,
	   MS_UDP_RECV_KEY = 1,
	   MS_UDP_REQUEST_KEY = 2,
	   MS_UDP_RECV_KEY_GRATUITOUS = 3,
	   MS_UDP_SYNC = 4 };

typedef struct {
	uint32_t count;
	uint32_t keys;
	uint8_t plugin;
	uint8_t valid;
} MsInput;

typedef struct {
	uint32_t reg_id; /* 0 = seat empty */
	uint8_t plugin, rawdata;
	int alive;				 /* saw a type-2 UDP packet since last sweep */
	struct sockaddr_in addr; /* learned from type-0 UDP packets */
	int addr_valid;
	MsInput inputs[MS_INPUT_RING];
	uint32_t pending_keys;
	uint8_t pending_plugin;
	/* per-tick accumulators for buffer management */
	double lag_sum, health_sum;
	uint32_t sample_n;
} MsPlayer;

typedef struct {
	char ext[16];
	uint8_t* data;
	uint32_t size;
	int valid;
} MsSave;
typedef struct {
	uint32_t vi;
	uint8_t blob[MS_CP0_BLOB];
	int valid;
} MsSync;

typedef enum { MS_DEFER_NONE = 0,
			   MS_DEFER_SETTINGS,
			   MS_DEFER_SAVE,
			   MS_DEFER_REG } MsDeferKind;

typedef struct {
	int fd;		  /* -1 = slot free */
	uint8_t* buf; /* accumulated unparsed bytes */
	size_t len, cap;
	uint8_t request;   /* MS_TCP_NONE when between requests */
	char save_ext[16]; /* parse state for SEND/RECV_SAVE */
	uint32_t save_size;
	/* deferred replies, FIFO */
	struct {
		MsDeferKind kind;
		char ext[16];
		long long since_ms;
	} defer[8];
	int defer_n;
} MsConn;

typedef struct {
	int tcp_fd, udp_fd;
	int port, expected_players, buffer_target;
	MsConn conns[MS_MAX_CONNS];
	MsPlayer players[MS_MAX_PLAYERS];
	MsSave saves[MS_MAX_SAVES];
	MsSync syncs[MS_SYNC_RING];
	uint8_t settings[MS_SETTINGS_SIZE];
	int has_settings;
	uint8_t status; /* bit0 desync, bits1-4 player disconnect */
	uint32_t lead_count;
	uint32_t buffer_size; /* server-side lookahead window, starts at 3 */
	int ever_registered;
} MsServer;

long long ms_now_ms(void);
void ms_log(const char* fmt, ...);
int ms_registered_count(const MsServer* s);
/* uint32 wrap-aware: returns nonzero when a is newer (larger) than b */
int ms_uint_larger(uint32_t a, uint32_t b);

/* tcp.c */
void ms_tcp_handle_conn(MsServer* s, MsConn* c); /* parse c->buf, act, queue defers */
void ms_tcp_service_deferred(MsServer* s);		 /* try to satisfy all queued defers */
void ms_conn_close(MsServer* s, MsConn* c);

/* udp.c */
void ms_udp_handle_packet(MsServer* s, const uint8_t* pkt, size_t len,
						  const struct sockaddr_in* from);
void ms_tick_buffer(MsServer* s); /* 1 s: buffer_size hysteresis */
int ms_tick_sweep(MsServer* s);	  /* 30 s: returns 1 when server should exit */

#endif
