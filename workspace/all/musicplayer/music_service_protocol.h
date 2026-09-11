#ifndef MUSIC_SERVICE_PROTOCOL_H
#define MUSIC_SERVICE_PROTOCOL_H

#include <stdint.h>

#define MUSIC_SERVICE_SOCKET_ENV "NX_MUSIC_SOCKET"
#define MUSIC_SERVICE_DEFAULT_SOCKET "/tmp/trimui_music/control.sock"
#define MUSIC_SERVICE_MAX_PATH 512
#define MUSIC_SERVICE_MAX_TITLE 256
#define MUSIC_SERVICE_MAX_ARTIST 256
#define MUSIC_SERVICE_MAX_ALBUM 256
#define MUSIC_SERVICE_MAX_ERROR 128
#define MUSIC_SERVICE_MAX_FRAME 8192
#define MUSIC_RESUME_SAVE_INTERVAL_MS 60000
#define MUSIC_SERVICE_PROTOCOL_VERSION 9u
#define MUSIC_VIS_BARS 64
#define MUSIC_VIS_SAMPLE_COUNT 1024
#define MUSIC_SERVICE_MAGIC 0x4e584d50u

typedef enum {
	MUSIC_CMD_SNAPSHOT = 1,
	MUSIC_CMD_LOAD = 2,
	MUSIC_CMD_SELECT = 3,
	MUSIC_CMD_PLAY = 4,
	MUSIC_CMD_PAUSE = 5,
	MUSIC_CMD_STOP = 6,
	MUSIC_CMD_TOGGLE = 7,
	MUSIC_CMD_NEXT = 8,
	MUSIC_CMD_PREVIOUS = 9,
	MUSIC_CMD_SEEK = 10,
	MUSIC_CMD_REPEAT = 11,
	MUSIC_CMD_SHUFFLE = 12,
	MUSIC_CMD_SHUTDOWN = 13,
	MUSIC_CMD_RADIO_LOAD = 14,
	MUSIC_CMD_PODCAST_LOAD = 15,
	MUSIC_CMD_LOAD_PLAYLIST = 16,
	MUSIC_CMD_SET_SHUFFLE = 17,
	MUSIC_CMD_PODCAST_PROGRESS = 18,
	MUSIC_CMD_PODCAST_MARK_PLAYED = 19,
	MUSIC_CMD_SET_SPEED = 20,
	MUSIC_CMD_AUDIO_SETTINGS = 21,
	MUSIC_CMD_SLEEP = 22,
	MUSIC_CMD_WAKE = 23,
	MUSIC_CMD_SET_VOLUME = 24
} MusicCommand;

enum {
	MUSIC_FORMAT_UNKNOWN = 0,
	MUSIC_FORMAT_WAV,
	MUSIC_FORMAT_MP3,
	MUSIC_FORMAT_OGG,
	MUSIC_FORMAT_FLAC,
	MUSIC_FORMAT_MOD,
	MUSIC_FORMAT_M4A,
	MUSIC_FORMAT_AAC,
	MUSIC_FORMAT_OPUS
};

enum {
	MUSIC_STATE_STOPPED = 0,
	MUSIC_STATE_PLAYING = 1,
	MUSIC_STATE_PAUSED = 2
};

enum {
	MUSIC_QUEUE_NONE = 0,
	MUSIC_QUEUE_FOLDER = 1,
	MUSIC_QUEUE_M3U = 2
};

enum {
	MUSIC_RADIO_STOPPED = 0,
	MUSIC_RADIO_CONNECTING,
	MUSIC_RADIO_BUFFERING,
	MUSIC_RADIO_PLAYING,
	MUSIC_RADIO_ERROR
};

typedef enum {
	MUSIC_SOURCE_NONE = 0,
	MUSIC_SOURCE_LOCAL = 1,
	MUSIC_SOURCE_RADIO = 2,
	MUSIC_SOURCE_PODCAST = 3
} MusicSource;

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint16_t version;
	uint16_t command;
	uint32_t request_id;
	uint32_t payload_length;
} MusicFrameHeader;

typedef struct __attribute__((packed)) {
	int32_t state;
	int32_t format;
	int32_t position_ms;
	int32_t duration_ms;
	float volume;
	int32_t repeat;
	int32_t shuffle;
	float playback_speed;
	int32_t source_sample_rate;
	int32_t output_sample_rate;
	int32_t audio_open;
	int32_t stream_eof;
	int32_t source;
	int32_t loaded;
	int32_t source_state;
	int32_t radio_bitrate;
	int32_t radio_buffer_percent;
	int32_t podcast_progress_sec;
	int32_t queue_count;
	int32_t queue_index;
	int32_t queue_kind;
	uint32_t capabilities;
	uint16_t visualization[MUSIC_VIS_BARS];
	int16_t visualization_samples[MUSIC_VIS_SAMPLE_COUNT];
	char artwork_path[MUSIC_SERVICE_MAX_PATH];
	char current_file[MUSIC_SERVICE_MAX_PATH];
	char queue_path[MUSIC_SERVICE_MAX_PATH];
	char title[MUSIC_SERVICE_MAX_TITLE];
	char artist[MUSIC_SERVICE_MAX_ARTIST];
	char album[MUSIC_SERVICE_MAX_ALBUM];
	char podcast_feed_url[MUSIC_SERVICE_MAX_PATH];
	char podcast_episode_guid[128];
} MusicSnapshotWire;

typedef struct __attribute__((packed)) {
	int32_t status;
	char error[MUSIC_SERVICE_MAX_ERROR];
	MusicSnapshotWire snapshot;
} MusicResponseWire;

typedef struct __attribute__((packed)) {
	char path[MUSIC_SERVICE_MAX_PATH];
	char selected_path[MUSIC_SERVICE_MAX_PATH];
} MusicLoadRequest;

typedef struct __attribute__((packed)) {
	int32_t value;
} MusicIntRequest;

typedef struct __attribute__((packed)) {
	char url[MUSIC_SERVICE_MAX_PATH];
} MusicRadioLoadRequest;

typedef struct __attribute__((packed)) {
	char path[MUSIC_SERVICE_MAX_PATH];
	int32_t index;
} MusicPlaylistLoadRequest;

typedef struct __attribute__((packed)) {
	char feed_url[MUSIC_SERVICE_MAX_PATH];
	char episode_guid[128];
} MusicPodcastLoadRequest;

typedef struct __attribute__((packed)) {
	char feed_url[MUSIC_SERVICE_MAX_PATH];
	char episode_guid[128];
	int32_t position_sec;
} MusicPodcastProgressRequest;

typedef struct __attribute__((packed)) {
	float speed;
} MusicSpeedRequest;

typedef struct __attribute__((packed)) {
	int32_t bass_filter_hz;
	float soft_limiter_threshold;
	int32_t rate_mode_follow;
	int32_t resampler_quality;
	int32_t buffer_frames;
} MusicAudioSettingsRequest;

#define MUSIC_STATUS_OK 0
#define MUSIC_STATUS_BAD_REQUEST -1
#define MUSIC_STATUS_NOT_FOUND -2
#define MUSIC_STATUS_UNAVAILABLE -3
#define MUSIC_STATUS_INTERNAL -4
#define MUSIC_SERVICE_TRANSPORT_ERROR -100
#define MUSIC_CAP_PLAY (1u << 0)
#define MUSIC_CAP_PAUSE (1u << 1)
#define MUSIC_CAP_NEXT (1u << 2)
#define MUSIC_CAP_PREVIOUS (1u << 3)
#define MUSIC_CAP_SEEK (1u << 4)

#endif
