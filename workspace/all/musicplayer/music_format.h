#ifndef MUSIC_FORMAT_H
#define MUSIC_FORMAT_H

#include <stdbool.h>

typedef enum {
	AUDIO_FORMAT_UNKNOWN = 0,
	AUDIO_FORMAT_WAV,
	AUDIO_FORMAT_MP3,
	AUDIO_FORMAT_OGG,
	AUDIO_FORMAT_FLAC,
	AUDIO_FORMAT_MOD,
	AUDIO_FORMAT_M4A,
	AUDIO_FORMAT_AAC,
	AUDIO_FORMAT_OPUS
} AudioFormat;

AudioFormat MusicFormat_detect(const char* filepath);
bool MusicFormat_isSupported(const char* filepath);

#endif
