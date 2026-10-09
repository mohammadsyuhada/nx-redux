#include "ma_core_progress.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

#define CORE_PROGRESS_FRESH_MS 600

static pthread_mutex_t progress_lock = PTHREAD_MUTEX_INITIALIZER;
static char progress_msg[512];
static unsigned long progress_at;
static int progress_set;

static unsigned long monotonic_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)ts.tv_sec * 1000UL + (unsigned long)(ts.tv_nsec / 1000000L);
}
static unsigned long (*now_ms)(void) = monotonic_ms;

void CoreProgress_setClock(unsigned long (*clock_ms)(void)) {
	now_ms = clock_ms ? clock_ms : monotonic_ms;
}

void CoreProgress_set(const char* msg) {
	pthread_mutex_lock(&progress_lock);
	if (msg && *msg) {
		strncpy(progress_msg, msg, sizeof(progress_msg) - 1);
		progress_msg[sizeof(progress_msg) - 1] = '\0';
		progress_at = now_ms();
		progress_set = 1;
	} else {
		progress_set = 0;
	}
	pthread_mutex_unlock(&progress_lock);
}

static void copy_clipped(char* dst, size_t size, const char* src, size_t len) {
	if (!size)
		return;
	if (len >= size)
		len = size - 1;
	memcpy(dst, src, len);
	dst[len] = '\0';
}

bool CoreProgress_active(char* title, size_t title_size, char* detail, size_t detail_size) {
	bool active = false;
	pthread_mutex_lock(&progress_lock);
	if (progress_set && now_ms() - progress_at < CORE_PROGRESS_FRESH_MS) {
		const char* nl = strchr(progress_msg, '\n');
		size_t title_len = nl ? (size_t)(nl - progress_msg) : strlen(progress_msg);
		copy_clipped(title, title_size, progress_msg, title_len);
		const char* rest = nl ? nl + 1 : "";
		copy_clipped(detail, detail_size, rest, strlen(rest));
		active = true;
	}
	pthread_mutex_unlock(&progress_lock);
	return active;
}
