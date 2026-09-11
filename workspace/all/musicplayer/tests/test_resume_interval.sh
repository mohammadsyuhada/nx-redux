#!/bin/sh
set -eu

grep -qx '#define MUSIC_RESUME_SAVE_INTERVAL_MS 60000' music_service_protocol.h
grep -Fq 'now - local_last_resume_ms >= MUSIC_RESUME_SAVE_INTERVAL_MS' musicplayerd.c
