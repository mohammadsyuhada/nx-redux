#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "defines.h"
#include "ui_icons.h"
#include "ui/ui_icons.h" // common icon helpers (local header shadows the name)

#define ICON_FOLDER RES_PATH "/icon-folder.png"
#define ICON_AUDIO RES_PATH "/icon-audio.png"
#define ICON_PLAY_ALL RES_PATH "/icon-play-all.png"
#define ICON_MP3 RES_PATH "/icon-mp3.png"
#define ICON_FLAC RES_PATH "/icon-flac.png"
#define ICON_OGG RES_PATH "/icon-ogg.png"
#define ICON_WAV RES_PATH "/icon-wav.png"
#define ICON_M4A RES_PATH "/icon-m4a.png"
#define ICON_AAC RES_PATH "/icon-aac.png"
#define ICON_OPUS RES_PATH "/icon-ops.png"
#define ICON_COMPLETE RES_PATH "/icon-complete.png"
#define ICON_DOWNLOAD RES_PATH "/icon-download.png"

// Icon storage - original (black) and inverted (white) versions
typedef struct {
	SDL_Surface* folder;
	SDL_Surface* folder_inv;
	SDL_Surface* audio;
	SDL_Surface* audio_inv;
	SDL_Surface* play_all;
	SDL_Surface* play_all_inv;
	SDL_Surface* mp3;
	SDL_Surface* mp3_inv;
	SDL_Surface* flac;
	SDL_Surface* flac_inv;
	SDL_Surface* ogg;
	SDL_Surface* ogg_inv;
	SDL_Surface* wav;
	SDL_Surface* wav_inv;
	SDL_Surface* m4a;
	SDL_Surface* m4a_inv;
	SDL_Surface* aac;
	SDL_Surface* aac_inv;
	SDL_Surface* opus;
	SDL_Surface* opus_inv;
	// Podcast badge icons
	SDL_Surface* complete;
	SDL_Surface* complete_inv;
	SDL_Surface* download;
	SDL_Surface* download_inv;
	bool loaded;
} IconSet;

static IconSet icons = {0};

// Initialize icons
void Icons_init(void) {
	if (icons.loaded)
		return;

	UI_loadIconPair(ICON_FOLDER, &icons.folder, &icons.folder_inv);
	UI_loadIconPair(ICON_AUDIO, &icons.audio, &icons.audio_inv);
	UI_loadIconPair(ICON_PLAY_ALL, &icons.play_all, &icons.play_all_inv);
	UI_loadIconPair(ICON_MP3, &icons.mp3, &icons.mp3_inv);
	UI_loadIconPair(ICON_FLAC, &icons.flac, &icons.flac_inv);
	UI_loadIconPair(ICON_OGG, &icons.ogg, &icons.ogg_inv);
	UI_loadIconPair(ICON_WAV, &icons.wav, &icons.wav_inv);
	UI_loadIconPair(ICON_M4A, &icons.m4a, &icons.m4a_inv);
	UI_loadIconPair(ICON_AAC, &icons.aac, &icons.aac_inv);
	UI_loadIconPair(ICON_OPUS, &icons.opus, &icons.opus_inv);
	// Podcast badge icons
	UI_loadIconPair(ICON_COMPLETE, &icons.complete, &icons.complete_inv);
	UI_loadIconPair(ICON_DOWNLOAD, &icons.download, &icons.download_inv);
	UI_initEmptyIcon();

	// Consider loaded if at least folder icon exists
	icons.loaded = (icons.folder != NULL);
}

// Cleanup icons
void Icons_quit(void) {
	UI_freeIconPair(&icons.folder, &icons.folder_inv);
	UI_freeIconPair(&icons.audio, &icons.audio_inv);
	UI_freeIconPair(&icons.play_all, &icons.play_all_inv);
	UI_freeIconPair(&icons.mp3, &icons.mp3_inv);
	UI_freeIconPair(&icons.flac, &icons.flac_inv);
	UI_freeIconPair(&icons.ogg, &icons.ogg_inv);
	UI_freeIconPair(&icons.wav, &icons.wav_inv);
	UI_freeIconPair(&icons.m4a, &icons.m4a_inv);
	UI_freeIconPair(&icons.aac, &icons.aac_inv);
	UI_freeIconPair(&icons.opus, &icons.opus_inv);
	UI_freeIconPair(&icons.complete, &icons.complete_inv);
	UI_freeIconPair(&icons.download, &icons.download_inv);
	UI_quitEmptyIcon();
	icons.loaded = false;
}

// Check if icons are loaded
bool Icons_isLoaded(void) {
	return icons.loaded;
}

// Get folder icon
SDL_Surface* Icons_getFolder(bool selected) {
	if (!icons.loaded)
		return NULL;
	return selected ? icons.folder : icons.folder_inv;
}

// Get play all icon
SDL_Surface* Icons_getPlayAll(bool selected) {
	if (!icons.loaded)
		return NULL;
	return selected ? icons.play_all : icons.play_all_inv;
}

// Get icon for specific audio format
// Falls back to generic audio icon if format-specific icon not available
SDL_Surface* Icons_getForFormat(AudioFormat format, bool selected) {
	if (!icons.loaded)
		return NULL;

	SDL_Surface* icon = NULL;
	SDL_Surface* icon_inv = NULL;

	switch (format) {
	case AUDIO_FORMAT_MP3:
		icon = icons.mp3;
		icon_inv = icons.mp3_inv;
		break;
	case AUDIO_FORMAT_FLAC:
		icon = icons.flac;
		icon_inv = icons.flac_inv;
		break;
	case AUDIO_FORMAT_OGG:
		icon = icons.ogg;
		icon_inv = icons.ogg_inv;
		break;
	case AUDIO_FORMAT_WAV:
		icon = icons.wav;
		icon_inv = icons.wav_inv;
		break;
	case AUDIO_FORMAT_M4A:
		icon = icons.m4a;
		icon_inv = icons.m4a_inv;
		break;
	case AUDIO_FORMAT_AAC:
		icon = icons.aac;
		icon_inv = icons.aac_inv;
		break;
	case AUDIO_FORMAT_OPUS:
		icon = icons.opus;
		icon_inv = icons.opus_inv;
		break;
	default:
		// Fall back to generic audio icon
		icon = icons.audio;
		icon_inv = icons.audio_inv;
		break;
	}

	// If format-specific icon not loaded, fall back to generic
	if (!icon) {
		icon = icons.audio;
		icon_inv = icons.audio_inv;
	}

	return selected ? icon : icon_inv;
}

// Get complete/played badge icon
SDL_Surface* Icons_getComplete(bool selected) {
	if (!icons.loaded)
		return NULL;
	return selected ? icons.complete : icons.complete_inv;
}

// Get download badge icon
SDL_Surface* Icons_getDownload(bool selected) {
	if (!icons.loaded)
		return NULL;
	return selected ? icons.download : icons.download_inv;
}
