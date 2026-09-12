#ifndef MUSIC_SERVICE_SERVER_H
#define MUSIC_SERVICE_SERVER_H

#include "music_service_protocol.h"
#include <stddef.h>
#include <stdint.h>

typedef void (*MusicServiceCommandHandler)(uint16_t command, const unsigned char* payload,
										   size_t length, MusicResponseWire* response);

int MusicServiceServer_open(void);
void MusicServiceServer_poll(MusicServiceCommandHandler handler, int timeout_ms);
void MusicServiceServer_close(void);

#endif
