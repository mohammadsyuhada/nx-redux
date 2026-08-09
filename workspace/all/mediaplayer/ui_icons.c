#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "vp_defines.h"
#include "ui_icons.h"
#include "ui/ui_icons.h" // common icon helpers (local header shadows the name)

#define ICON_FOLDER RES_PATH "/icon-folder.png"
#define ICON_VIDEO RES_PATH "/icon-video.png"
#define ICON_MP4 RES_PATH "/icon-mp4.png"
#define ICON_MKV RES_PATH "/icon-mkv.png"
#define ICON_AVI RES_PATH "/icon-avi.png"
#define ICON_MOV RES_PATH "/icon-mov.png"
#define ICON_FLV RES_PATH "/icon-flv.png"
#define ICON_M4V RES_PATH "/icon-m4v.png"
#define ICON_WMV RES_PATH "/icon-wmv.png"
#define ICON_MPG RES_PATH "/icon-mpg.png"
#define ICON_MPEG RES_PATH "/icon-mpeg.png"
#define ICON_3GP RES_PATH "/icon-3gp.png"

// Icon storage - original (black) and inverted (white) versions
typedef struct {
	SDL_Surface* folder;
	SDL_Surface* folder_inv;
	SDL_Surface* video;
	SDL_Surface* video_inv;
	SDL_Surface* mp4;
	SDL_Surface* mp4_inv;
	SDL_Surface* mkv;
	SDL_Surface* mkv_inv;
	SDL_Surface* avi;
	SDL_Surface* avi_inv;
	SDL_Surface* mov;
	SDL_Surface* mov_inv;
	SDL_Surface* flv;
	SDL_Surface* flv_inv;
	SDL_Surface* m4v;
	SDL_Surface* m4v_inv;
	SDL_Surface* wmv;
	SDL_Surface* wmv_inv;
	SDL_Surface* mpg;
	SDL_Surface* mpg_inv;
	SDL_Surface* mpeg;
	SDL_Surface* mpeg_inv;
	SDL_Surface* _3gp;
	SDL_Surface* _3gp_inv;
	bool loaded;
} IconSet;

static IconSet icons = {0};

// Initialize icons
void Icons_init(void) {
	if (icons.loaded)
		return;

	UI_loadIconPair(ICON_FOLDER, &icons.folder, &icons.folder_inv);
	UI_loadIconPair(ICON_VIDEO, &icons.video, &icons.video_inv);
	UI_loadIconPair(ICON_MP4, &icons.mp4, &icons.mp4_inv);
	UI_loadIconPair(ICON_MKV, &icons.mkv, &icons.mkv_inv);
	UI_loadIconPair(ICON_AVI, &icons.avi, &icons.avi_inv);
	UI_loadIconPair(ICON_MOV, &icons.mov, &icons.mov_inv);
	UI_loadIconPair(ICON_FLV, &icons.flv, &icons.flv_inv);
	UI_loadIconPair(ICON_M4V, &icons.m4v, &icons.m4v_inv);
	UI_loadIconPair(ICON_WMV, &icons.wmv, &icons.wmv_inv);
	UI_loadIconPair(ICON_MPG, &icons.mpg, &icons.mpg_inv);
	UI_loadIconPair(ICON_MPEG, &icons.mpeg, &icons.mpeg_inv);
	UI_loadIconPair(ICON_3GP, &icons._3gp, &icons._3gp_inv);
	UI_initEmptyIcon();

	// Consider loaded if at least folder icon exists
	icons.loaded = (icons.folder != NULL);
}

// Cleanup icons
void Icons_quit(void) {
	UI_freeIconPair(&icons.folder, &icons.folder_inv);
	UI_freeIconPair(&icons.video, &icons.video_inv);
	UI_freeIconPair(&icons.mp4, &icons.mp4_inv);
	UI_freeIconPair(&icons.mkv, &icons.mkv_inv);
	UI_freeIconPair(&icons.avi, &icons.avi_inv);
	UI_freeIconPair(&icons.mov, &icons.mov_inv);
	UI_freeIconPair(&icons.flv, &icons.flv_inv);
	UI_freeIconPair(&icons.m4v, &icons.m4v_inv);
	UI_freeIconPair(&icons.wmv, &icons.wmv_inv);
	UI_freeIconPair(&icons.mpg, &icons.mpg_inv);
	UI_freeIconPair(&icons.mpeg, &icons.mpeg_inv);
	UI_freeIconPair(&icons._3gp, &icons._3gp_inv);
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

// Get icon for specific video format
// Falls back to generic video icon if format-specific icon not available
SDL_Surface* Icons_getForFormat(VideoFormat format, bool selected) {
	if (!icons.loaded)
		return NULL;

	SDL_Surface* icon = NULL;
	SDL_Surface* icon_inv = NULL;

	switch (format) {
	case VIDEO_FORMAT_MP4:
		icon = icons.mp4;
		icon_inv = icons.mp4_inv;
		break;
	case VIDEO_FORMAT_MKV:
		icon = icons.mkv;
		icon_inv = icons.mkv_inv;
		break;
	case VIDEO_FORMAT_AVI:
		icon = icons.avi;
		icon_inv = icons.avi_inv;
		break;
	case VIDEO_FORMAT_MOV:
		icon = icons.mov;
		icon_inv = icons.mov_inv;
		break;
	case VIDEO_FORMAT_FLV:
		icon = icons.flv;
		icon_inv = icons.flv_inv;
		break;
	case VIDEO_FORMAT_M4V:
		icon = icons.m4v;
		icon_inv = icons.m4v_inv;
		break;
	case VIDEO_FORMAT_WMV:
		icon = icons.wmv;
		icon_inv = icons.wmv_inv;
		break;
	case VIDEO_FORMAT_MPG:
		icon = icons.mpg;
		icon_inv = icons.mpg_inv;
		break;
	case VIDEO_FORMAT_3GP:
		icon = icons._3gp;
		icon_inv = icons._3gp_inv;
		break;
	default:
		// WEBM, TS, UNKNOWN -> generic video icon
		icon = icons.video;
		icon_inv = icons.video_inv;
		break;
	}

	// If format-specific icon not loaded, fall back to generic
	if (!icon) {
		icon = icons.video;
		icon_inv = icons.video_inv;
	}

	return selected ? icon : icon_inv;
}
