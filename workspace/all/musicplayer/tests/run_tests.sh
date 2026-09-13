#!/bin/sh
# Host-side unit tests for musicplayer/. No SDL, no cross toolchain.
# The two device tests here (test_music_service.c, test_music_client.c) are
# cross-compiled and run on a Brick by scripts/tests/test-music-service-e2e.sh,
# not from this runner.
set -eu
cd "$(dirname "$0")"
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -o /tmp/nx_test_music_balance ../music_balance.c test_music_balance.c && /tmp/nx_test_music_balance
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -o /tmp/nx_test_music_request_validation ../music_request_validation.c test_music_request_validation.c && /tmp/nx_test_music_request_validation
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -o /tmp/nx_test_resume ../resume.c test_resume.c && /tmp/nx_test_resume
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -I../../include -o /tmp/nx_test_radio_catalog ../radio_catalog_parse.c ../../include/parson/parson.c test_radio_catalog.c && /tmp/nx_test_radio_catalog
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -I../../../tg5040/platform -DMUSIC_SETTINGS_TEST_DIR='"/tmp/nx_music_settings_roundtrip"' -o /tmp/nx_test_music_settings ../settings.c test_music_settings.c -lm && /tmp/nx_test_music_settings
