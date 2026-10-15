#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

void selectScaler(int src_w, int src_h, int src_p);
void video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch);
void Video_cleanup(void);
int Video_ffFrameDue(void);
