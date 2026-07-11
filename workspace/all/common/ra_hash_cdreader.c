#include "ra_hash_cdreader.h"
#include "chd_reader.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*****************************************************************************
 * CHD (compressed hunks of data) reader support for disc images
 * 
 * The default rcheevos CD reader only supports CUE/BIN and ISO formats.
 * We wrap the CD reader callbacks to try CHD first, then fall back to default.
 * 
 * We use a wrapper handle to track whether a handle came from CHD or default
 * reader, so we can route subsequent calls to the correct implementation.
 *****************************************************************************/

// Store default CD reader callbacks for fallback
static rc_hash_cdreader_t ra_default_cdreader;

// Wrapper handle to distinguish CHD vs default reader handles
#define RA_CDHANDLE_MAGIC 0x43484448 // "CHDH"
typedef struct {
	uint32_t magic;		// Magic number to identify our wrapper
	bool is_chd;		// true = CHD handle, false = default reader handle
	void* inner_handle; // The actual handle from CHD or default reader
} ra_cdreader_handle_t;

// Helper to create a wrapper handle
static void* ra_cdreader_wrap_handle(void* inner_handle, bool is_chd) {
	if (!inner_handle)
		return NULL;

	ra_cdreader_handle_t* wrapper = (ra_cdreader_handle_t*)malloc(sizeof(ra_cdreader_handle_t));
	if (!wrapper) {
		// Failed to allocate wrapper - close the inner handle
		if (is_chd) {
			chd_close_track(inner_handle);
		} else if (ra_default_cdreader.close_track) {
			ra_default_cdreader.close_track(inner_handle);
		}
		return NULL;
	}

	wrapper->magic = RA_CDHANDLE_MAGIC;
	wrapper->is_chd = is_chd;
	wrapper->inner_handle = inner_handle;
	return wrapper;
}

// Helper to validate and unwrap handle
static ra_cdreader_handle_t* ra_cdreader_unwrap(void* handle) {
	if (!handle)
		return NULL;
	ra_cdreader_handle_t* wrapper = (ra_cdreader_handle_t*)handle;
	if (wrapper->magic != RA_CDHANDLE_MAGIC)
		return NULL;
	return wrapper;
}

// Wrapper: Try CHD first, then default
static void* ra_cdreader_open_track(const char* path, uint32_t track) {
	// Try CHD reader first
	void* handle = chd_open_track(path, track);
	if (handle) {
		return ra_cdreader_wrap_handle(handle, true);
	}
	// Fall back to default reader
	if (ra_default_cdreader.open_track) {
		handle = ra_default_cdreader.open_track(path, track);
		if (handle) {
			return ra_cdreader_wrap_handle(handle, false);
		}
	}
	return NULL;
}

static void* ra_cdreader_open_track_iterator(const char* path, uint32_t track, const rc_hash_iterator_t* iterator) {
	// Try CHD reader first
	void* handle = chd_open_track_iterator(path, track, iterator);
	if (handle) {
		return ra_cdreader_wrap_handle(handle, true);
	}
	// Fall back to default reader
	if (ra_default_cdreader.open_track_iterator) {
		handle = ra_default_cdreader.open_track_iterator(path, track, iterator);
		if (handle) {
			return ra_cdreader_wrap_handle(handle, false);
		}
	}
	if (ra_default_cdreader.open_track) {
		handle = ra_default_cdreader.open_track(path, track);
		if (handle) {
			return ra_cdreader_wrap_handle(handle, false);
		}
	}
	return NULL;
}

static size_t ra_cdreader_read_sector(void* track_handle, uint32_t sector, void* buffer, size_t requested_bytes) {
	ra_cdreader_handle_t* wrapper = ra_cdreader_unwrap(track_handle);
	if (!wrapper)
		return 0;

	if (wrapper->is_chd) {
		return chd_read_sector(wrapper->inner_handle, sector, buffer, requested_bytes);
	} else if (ra_default_cdreader.read_sector) {
		return ra_default_cdreader.read_sector(wrapper->inner_handle, sector, buffer, requested_bytes);
	}
	return 0;
}

static void ra_cdreader_close_track(void* track_handle) {
	ra_cdreader_handle_t* wrapper = ra_cdreader_unwrap(track_handle);
	if (!wrapper)
		return;

	if (wrapper->is_chd) {
		chd_close_track(wrapper->inner_handle);
	} else if (ra_default_cdreader.close_track) {
		ra_default_cdreader.close_track(wrapper->inner_handle);
	}

	// Clear magic and free wrapper
	wrapper->magic = 0;
	free(wrapper);
}

static uint32_t ra_cdreader_first_track_sector(void* track_handle) {
	ra_cdreader_handle_t* wrapper = ra_cdreader_unwrap(track_handle);
	if (!wrapper)
		return 0;

	if (wrapper->is_chd) {
		return chd_first_track_sector(wrapper->inner_handle);
	} else if (ra_default_cdreader.first_track_sector) {
		return ra_default_cdreader.first_track_sector(wrapper->inner_handle);
	}
	return 0;
}

void RA_HashCdreader_get(rc_hash_cdreader_t* out) {
	// Capture rcheevos' default reader once, for the fallback path
	rc_hash_get_default_cdreader(&ra_default_cdreader);

	memset(out, 0, sizeof(*out));
	out->open_track = ra_cdreader_open_track;
	out->open_track_iterator = ra_cdreader_open_track_iterator;
	out->read_sector = ra_cdreader_read_sector;
	out->close_track = ra_cdreader_close_track;
	out->first_track_sector = ra_cdreader_first_track_sector;
}
