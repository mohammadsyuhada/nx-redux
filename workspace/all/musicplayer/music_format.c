#include "music_format.h"
#include <strings.h>
#include <string.h>

AudioFormat MusicFormat_detect(const char* filepath) {
	if (!filepath)
		return AUDIO_FORMAT_UNKNOWN;
	const char* ext = strrchr(filepath, '.');
	if (!ext)
		return AUDIO_FORMAT_UNKNOWN;
	ext++;
	if (strcasecmp(ext, "mp3") == 0)
		return AUDIO_FORMAT_MP3;
	if (strcasecmp(ext, "wav") == 0)
		return AUDIO_FORMAT_WAV;
	if (strcasecmp(ext, "ogg") == 0)
		return AUDIO_FORMAT_OGG;
	if (strcasecmp(ext, "opus") == 0)
		return AUDIO_FORMAT_OPUS;
	if (strcasecmp(ext, "flac") == 0)
		return AUDIO_FORMAT_FLAC;
	if (strcasecmp(ext, "m4a") == 0)
		return AUDIO_FORMAT_M4A;
	if (strcasecmp(ext, "aac") == 0)
		return AUDIO_FORMAT_AAC;
	if (strcasecmp(ext, "mod") == 0 || strcasecmp(ext, "xm") == 0 ||
		strcasecmp(ext, "s3m") == 0 || strcasecmp(ext, "it") == 0)
		return AUDIO_FORMAT_MOD;
	return AUDIO_FORMAT_UNKNOWN;
}

bool MusicFormat_isSupported(const char* filepath) {
	return MusicFormat_detect(filepath) != AUDIO_FORMAT_UNKNOWN;
}
