#ifndef MUSIC_REQUEST_VALIDATION_H
#define MUSIC_REQUEST_VALIDATION_H

#include "music_service_protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool MusicRequest_isValidPayload(uint16_t command, const void* payload, size_t length);

#endif
