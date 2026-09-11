// Platform-linked lifecycle and snapshot regression test.
// Build with `make PLATFORM=tg5040 test_snapshot` (or tg5050), then run the
// resulting ELF on a device with Music Player's normal shared libraries.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define SDL_INIT_AUDIO 0x00000010u
extern void SDL_QuitSubSystem(uint32_t flags);

#include "../player.h"
#include "../settings.h"
#include "../../common/audio_manager.h"

void InitSettings(void);

static void write_le16(uint8_t* p, uint16_t value) {
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t* p, uint32_t value) {
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static int write_fixture_wav(const char* path) {
	const uint32_t frames = 44100 * 4;
	const uint16_t channels = 2;
	const uint16_t bits = 16;
	const uint32_t rate = 44100;
	const uint32_t data_size = frames * channels * sizeof(int16_t);
	uint8_t header[44] = {0};
	memcpy(header, "RIFF", 4);
	write_le32(header + 4, 36 + data_size);
	memcpy(header + 8, "WAVEfmt ", 8);
	write_le32(header + 16, 16);
	write_le16(header + 20, 1);
	write_le16(header + 22, channels);
	write_le32(header + 24, rate);
	write_le32(header + 28, rate * channels * bits / 8);
	write_le16(header + 32, channels * bits / 8);
	write_le16(header + 34, bits);
	memcpy(header + 36, "data", 4);
	write_le32(header + 40, data_size);

	FILE* file = fopen(path, "wb");
	if (!file)
		return -1;
	int ok = fwrite(header, sizeof(header), 1, file) == 1;
	for (uint32_t i = 0; ok && i < frames; i++) {
		int16_t sample = (int16_t)(((i * 97) % 2000) - 1000);
		int16_t frame[2] = {sample, (int16_t)-sample};
		ok = fwrite(frame, sizeof(frame), 1, file) == 1;
	}
	fclose(file);
	return ok ? 0 : -1;
}

typedef struct {
	int failed;
} SnapshotReader;

static void* read_snapshots(void* argument) {
	SnapshotReader* reader = argument;
	for (int i = 0; i < 2000; i++) {
		PlayerSnapshot snapshot;
		if (Player_getSnapshot(&snapshot) != 0 ||
			!memchr(snapshot.current_file, '\0', sizeof(snapshot.current_file)) ||
			!memchr(snapshot.track_info.title, '\0', sizeof(snapshot.track_info.title))) {
			reader->failed = 1;
			break;
		}
	}
	return NULL;
}

static int fail(const char* message, const char* fixture) {
	fprintf(stderr, "snapshot lifecycle test: %s\n", message);
	Player_quit();
	unlink(fixture);
	return 1;
}

#define CHECK(condition, message)            \
	do {                                     \
		if (!(condition))                    \
			return fail((message), fixture); \
	} while (0)

int main(void) {
	char fixture[128];
	snprintf(fixture, sizeof(fixture), "/tmp/nx_player_snapshot_%ld.wav", (long)getpid());
	if (write_fixture_wav(fixture) != 0)
		return 1;

	InitSettings();
	Settings_init();
	CHECK(Player_coreInit() == 0, "core initialization failed");
	Player_setVolume(0.0f);
	CHECK(Player_openAudioDevice() == 0, "audio device open failed");

	PlayerSnapshot initial;
	CHECK(Player_getSnapshot(&initial) == 0, "initial snapshot failed");
	CHECK(initial.audio_open, "initial snapshot did not report an open device");
	initial.current_file[0] = 'X';
	PlayerSnapshot independent;
	CHECK(Player_getSnapshot(&independent) == 0, "independent snapshot failed");
	CHECK(independent.current_file[0] == '\0', "snapshot exposed mutable engine storage");

	for (int i = 0; i < 3; i++) {
		Player_closeAudioDevice();
		PlayerSnapshot closed;
		CHECK(Player_getSnapshot(&closed) == 0 && !closed.audio_open,
			  "close did not publish a closed device");
		CHECK(Player_openAudioDevice() == 0, "repeated device reopen failed");
	}

	Player_setSampleRate(44100);
	Player_closeAudioDevice();
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	CHECK(Player_load(fixture) != 0, "failed-open load unexpectedly succeeded");
	PlayerSnapshot failed_load;
	CHECK(Player_getSnapshot(&failed_load) == 0 && !failed_load.audio_open &&
			  failed_load.state == PLAYER_STATE_STOPPED &&
			  failed_load.current_file[0] == '\0' && failed_load.track_info.title[0] == '\0',
		  "failed-open load published stale playback metadata");
	CHECK(Player_play() != 0, "failed-open load published a decoder");
	CHECK(Player_reopenAudioDevice() == 0, "device recovery after failed load failed");

	CHECK(Player_load(fixture) == 0, "fixture load failed after recovery");
	PlayerSnapshot loaded;
	CHECK(Player_getSnapshot(&loaded) == 0, "loaded snapshot failed");
	CHECK(loaded.track_info.duration_ms > 0, "loaded metadata was not copied");
	CHECK(loaded.current_file[0] != '\0', "loaded identity was not copied");
	char missing_fixture[128];
	snprintf(missing_fixture, sizeof(missing_fixture), "/tmp/nx_missing_player_%ld.wav", (long)getpid());
	CHECK(Player_load(missing_fixture) != 0, "missing load unexpectedly succeeded");
	PlayerSnapshot rejected_load;
	CHECK(Player_getSnapshot(&rejected_load) == 0 && rejected_load.state == PLAYER_STATE_STOPPED &&
			  rejected_load.current_file[0] == '\0' && rejected_load.track_info.title[0] == '\0',
		  "failed load retained stale metadata");
	CHECK(Player_load(fixture) == 0, "fixture reload after failed load failed");
	Player_seek(1000);
	PlayerSnapshot idle_seek;
	CHECK(!Player_resume() && Player_getSnapshot(&idle_seek) == 0 &&
			  idle_seek.state == PLAYER_STATE_STOPPED && idle_seek.position_ms == 1000,
		  "idle decoder seek completes before playback opens audio");

	CHECK(Player_play() == 0, "play failed");
	SnapshotReader readers[4] = {{0}, {0}, {0}, {0}};
	pthread_t threads[4];
	for (int i = 0; i < 4; i++)
		CHECK(pthread_create(&threads[i], NULL, read_snapshots, &readers[i]) == 0,
			  "snapshot reader creation failed");
	usleep(100000);
	for (int i = 0; i < 4; i++) {
		pthread_join(threads[i], NULL);
		CHECK(!readers[i].failed, "concurrent snapshot read failed");
	}
	usleep(200000);

	PlayerSnapshot before_rate;
	CHECK(Player_getSnapshot(&before_rate) == 0 && before_rate.position_ms > 0,
		  "fixture did not advance before rate transition");
	int alternate_rate = before_rate.output_sample_rate == 44100 ? 48000 : 44100;
	int published_rate = AudioMgr_pickRate(alternate_rate);
	Player_setSampleRate(alternate_rate);
	PlayerSnapshot after_rate;
	CHECK(Player_getSnapshot(&after_rate) == 0 &&
			  after_rate.output_sample_rate == published_rate &&
			  after_rate.state == PLAYER_STATE_PLAYING && !after_rate.stream_eof,
		  "sample-rate transition did not reopen at the published sink rate");
	CHECK(abs(after_rate.position_ms - before_rate.position_ms) < 300,
		  "sample-rate transition jumped source-time position");

	usleep(150000);
	PlayerSnapshot before_sink_reopen;
	CHECK(Player_getSnapshot(&before_sink_reopen) == 0 && before_sink_reopen.position_ms > after_rate.position_ms,
		  "playback did not continue after sample-rate transition");
	CHECK(Player_reopenAudioDevice() == 0, "sink reopen failed");
	PlayerSnapshot after_sink_reopen;
	CHECK(Player_getSnapshot(&after_sink_reopen) == 0 &&
			  after_sink_reopen.state == PLAYER_STATE_PLAYING && !after_sink_reopen.stream_eof,
		  "sink reopen did not restart the live stream");
	CHECK(abs(after_sink_reopen.position_ms - before_sink_reopen.position_ms) < 300,
		  "sink reopen jumped source-time position");

	Player_closeAudioDevice();
	usleep(100000);
	CHECK(Player_openAudioDevice() == 0, "playing close/reopen failed");
	PlayerSnapshot playing;
	CHECK(Player_getSnapshot(&playing) == 0 && playing.state == PLAYER_STATE_PLAYING && !playing.stream_eof,
		  "playing close/open did not restart the decoder");
	CHECK(abs(playing.position_ms - after_sink_reopen.position_ms) < 400,
		  "explicit close/open discarded source-time position");

	Player_pause();
	PlayerSnapshot paused_before_close;
	CHECK(Player_getSnapshot(&paused_before_close) == 0, "paused snapshot failed");
	Player_closeAudioDevice();
	usleep(100000);
	CHECK(Player_openAudioDevice() == 0, "paused close/reopen failed");
	PlayerSnapshot paused;
	CHECK(Player_getSnapshot(&paused) == 0 && paused.state == PLAYER_STATE_PAUSED,
		  "pause intent was resumed after reopen");
	CHECK(abs(paused.position_ms - paused_before_close.position_ms) < 50,
		  "paused closed device advanced playback position");

	CHECK(Player_play() == 0, "play failed for EOF coverage");
	bool reached_eof = false;
	for (int i = 0; i < 600; i++) {
		PlayerSnapshot eof_snapshot;
		CHECK(Player_getSnapshot(&eof_snapshot) == 0, "EOF snapshot failed");
		if (eof_snapshot.stream_eof) {
			reached_eof = true;
			break;
		}
		usleep(10000);
	}
	CHECK(reached_eof, "fixture did not reach decoder EOF");
	Player_seek(0);
	bool reprimed = false;
	for (int i = 0; i < 100; i++) {
		PlayerSnapshot reprime_snapshot;
		CHECK(Player_getSnapshot(&reprime_snapshot) == 0, "reprime snapshot failed");
		if (!reprime_snapshot.stream_eof) {
			reprimed = true;
			break;
		}
		usleep(10000);
	}
	CHECK(reprimed, "seek after EOF did not reprime the decoder");
	CHECK(Player_play() == 0, "reprimed playback failed to start");
	bool resumed_from_reprime = false;
	for (int i = 0; i < 100; i++) {
		PlayerSnapshot resumed;
		CHECK(Player_getSnapshot(&resumed) == 0, "reprimed playback snapshot failed");
		if (resumed.position_ms > 0 && !resumed.stream_eof) {
			resumed_from_reprime = true;
			break;
		}
		usleep(10000);
	}
	CHECK(resumed_from_reprime, "reprimed queue did not produce fresh playback data");

	Player_stop();
	Player_closeAudioDevice();
	CHECK(Player_openAudioDevice() == 0, "stopped close/reopen failed");
	PlayerSnapshot stopped;
	CHECK(Player_getSnapshot(&stopped) == 0 && stopped.state == PLAYER_STATE_STOPPED,
		  "stop intent was resumed after reopen");

	Player_quit();
	unlink(fixture);
	puts("player snapshot/lifecycle test passed");
	return 0;
}
