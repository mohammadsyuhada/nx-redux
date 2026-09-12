#include "ui_utils.h"

// Get format name string
const char* get_format_name(int format) {
	switch (format) {
	case MUSIC_FORMAT_MP3:
		return "MP3";
	case MUSIC_FORMAT_FLAC:
		return "FLAC";
	case MUSIC_FORMAT_OGG:
		return "OGG";
	case MUSIC_FORMAT_WAV:
		return "WAV";
	case MUSIC_FORMAT_MOD:
		return "MOD";
	case MUSIC_FORMAT_M4A:
		return "M4A";
	case MUSIC_FORMAT_AAC:
		return "AAC";
	case MUSIC_FORMAT_OPUS:
		return "OPUS";
	default:
		return "---";
	}
}
