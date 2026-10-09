// ma_core_progress.h - progress a core reports while it works before its first
// real frame (RETRO_MESSAGE_TYPE_PROGRESS), e.g. mupen64plus-next's GLideN64
// converting a hi-res texture pack on first launch. minarch shows it as a
// notice page instead of the game picture while it is fresh.
#ifndef MA_CORE_PROGRESS_H
#define MA_CORE_PROGRESS_H

#include <stdbool.h>
#include <stddef.h>

// Any thread (cores report from their own threads). "Title\ndetail"; an
// empty message ends the progress at once.
void CoreProgress_set(const char* msg);

// Main thread: true while the last message is under 600 ms old, with its
// first line in title and the rest in detail (both NUL-terminated, clipped).
bool CoreProgress_active(char* title, size_t title_size, char* detail, size_t detail_size);

// Tests only: replace the millisecond clock.
void CoreProgress_setClock(unsigned long (*clock_ms)(void));

#endif
