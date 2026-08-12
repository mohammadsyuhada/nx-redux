#ifndef __PLAYLIST_M3U_H__
#define __PLAYLIST_M3U_H__

#include <stdbool.h>
#include "playlist.h" // For PlaylistTrack

#define PLAYLISTS_DIR SHARED_USERDATA_PATH "/music-player/playlists"
// Kept under LISTDIALOG_MAX_ITEMS (128) minus the "+ New Playlist" row, since
// the add-to-playlist dialog builds playlist_count + 1 items.
#define MAX_PLAYLISTS 100
#define MAX_PLAYLIST_NAME 128

typedef struct {
	char name[MAX_PLAYLIST_NAME]; // Display name (without .m3u)
	char path[512];				  // Full path to .m3u file
	int track_count;			  // Number of tracks (from quick scan)
} PlaylistInfo;

// Create playlists directory if it doesn't exist
void M3U_init(void);

// Scan playlists directory, fill out array. Returns count.
int M3U_listPlaylists(PlaylistInfo* out, int max);

// Create an empty .m3u file with the given name. Returns 0 on success.
int M3U_create(const char* name);

// Delete a playlist file. Returns 0 on success.
int M3U_delete(const char* m3u_path);

// Rename a playlist file to "<new_name>.m3u" in PLAYLISTS_DIR.
// Refuses to overwrite an existing playlist. Returns 0 on success.
int M3U_rename(const char* m3u_path, const char* new_name);

// Append a track to an .m3u file. Returns 0 on success.
int M3U_addTrack(const char* m3u_path, const char* track_path, const char* display_name);

// Append multiple tracks in one pass, reading the existing paths ONCE for the
// duplicate check (per-track M3U_addTrack re-reads the whole file each call —
// O(n*m) for a folder add). Returns the number of tracks actually appended.
int M3U_addTracks(const char* m3u_path, const PlaylistTrack* tracks, int count);

// Rewrite the .m3u file without the track at the given index. Returns 0 on success.
int M3U_removeTrack(const char* m3u_path, int index);

// Load tracks from an .m3u file into a PlaylistTrack array.
// Skips missing files. Sets *count to number loaded. Returns 0 on success.
int M3U_loadTracks(const char* m3u_path, PlaylistTrack* tracks, int max, int* count);

// Check if a track path is already in the .m3u file.
bool M3U_containsTrack(const char* m3u_path, const char* track_path);

#endif
