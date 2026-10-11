// Host-compiled unit test for the HLS ID3 metadata parser in radio_hls.c.
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -I. radio_hls.c tag_meta.c tests/test_radio_hls_id3.c -o /tmp/test_radio_hls_id3 && /tmp/test_radio_hls_id3
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../radio_hls.h"
#include "../radio_net.h"

// radio_hls.c fetches variant playlists; the ID3 parser never does
int radio_net_fetch(const char* url, uint8_t* buffer, int buffer_size, char* content_type, int ct_size) {
	return -1;
}

static int failures;

#define CHECK(cond)                                                         \
	do {                                                                    \
		if (!(cond)) {                                                      \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                     \
		}                                                                   \
	} while (0)

// Build an ID3v2.3 tag holding the given frames
static int build_tag(uint8_t* out, const char* const* ids, const uint8_t* const* bodies,
					 const int* lens, int count) {
	int pos = 10;
	for (int i = 0; i < count; i++) {
		memcpy(out + pos, ids[i], 4);
		out[pos + 4] = (uint8_t)(lens[i] >> 24);
		out[pos + 5] = (uint8_t)(lens[i] >> 16);
		out[pos + 6] = (uint8_t)(lens[i] >> 8);
		out[pos + 7] = (uint8_t)lens[i];
		out[pos + 8] = out[pos + 9] = 0;
		memcpy(out + pos + 10, bodies[i], lens[i]);
		pos += 10 + lens[i];
	}
	int size = pos - 10;
	memcpy(out, "ID3\x03\x00\x00", 6);
	out[6] = (size >> 21) & 0x7F;
	out[7] = (size >> 14) & 0x7F;
	out[8] = (size >> 7) & 0x7F;
	out[9] = size & 0x7F;
	return pos;
}

static void test_utf16_title_artist(void) {
	// TIT2 双截棍, TPE1 周杰伦, both UTF-16LE with BOM
	const uint8_t tit2[] = {1, 0xFF, 0xFE, 0xCC, 0x53, 0x2A, 0x62, 0xCD, 0x68};
	const uint8_t tpe1[] = {1, 0xFF, 0xFE, 0x68, 0x54, 0x70, 0x67, 0x26, 0x4F};
	const char* ids[] = {"TIT2", "TPE1"};
	const uint8_t* bodies[] = {tit2, tpe1};
	int lens[] = {sizeof(tit2), sizeof(tpe1)};
	uint8_t tag[128];
	int n = build_tag(tag, ids, bodies, lens, 2);
	char artist[64], title[64];
	CHECK(radio_hls_parse_id3_metadata(tag, n, artist, sizeof(artist), title, sizeof(title)) == n);
	CHECK(strcmp(title, "\xE5\x8F\x8C\xE6\x88\xAA\xE6\xA3\x8D") == 0);
	CHECK(strcmp(artist, "\xE5\x91\xA8\xE6\x9D\xB0\xE4\xBC\xA6") == 0);
}

static void test_latin1_title(void) {
	const uint8_t tit2[] = {0, 'C', 0xE9, 'l', 'i', 'n', 'e'};
	const char* ids[] = {"TIT2"};
	const uint8_t* bodies[] = {tit2};
	int lens[] = {sizeof(tit2)};
	uint8_t tag[64];
	int n = build_tag(tag, ids, bodies, lens, 1);
	char artist[64], title[64];
	radio_hls_parse_id3_metadata(tag, n, artist, sizeof(artist), title, sizeof(title));
	CHECK(strcmp(title, "C\xC3\xA9line") == 0);
	CHECK(artist[0] == '\0');
}

static void test_txxx_utf16_stream_title(void) {
	// TXXX, UTF-16LE: description "StreamTitle", value "A - Bé"
	uint8_t txxx[64];
	int p = 0;
	txxx[p++] = 1;
	txxx[p++] = 0xFF;
	txxx[p++] = 0xFE;
	for (const char* s = "StreamTitle"; *s; s++) {
		txxx[p++] = (uint8_t)*s;
		txxx[p++] = 0;
	}
	txxx[p++] = 0;
	txxx[p++] = 0;
	txxx[p++] = 0xFF;
	txxx[p++] = 0xFE;
	const uint16_t value[] = {'A', ' ', '-', ' ', 'B', 0xE9};
	for (int i = 0; i < 6; i++) {
		txxx[p++] = (uint8_t)value[i];
		txxx[p++] = (uint8_t)(value[i] >> 8);
	}
	const char* ids[] = {"TXXX"};
	const uint8_t* bodies[] = {txxx};
	int lens[] = {p};
	uint8_t tag[128];
	int n = build_tag(tag, ids, bodies, lens, 1);
	char artist[64], title[64];
	radio_hls_parse_id3_metadata(tag, n, artist, sizeof(artist), title, sizeof(title));
	CHECK(strcmp(artist, "A") == 0);
	CHECK(strcmp(title, "B\xC3\xA9") == 0);
}

static void test_txxx_utf8_and_other_description(void) {
	const uint8_t hit[] = "\x03TITLE\0X - Y";
	const uint8_t miss[] = "\x03"
						   "COMMENT\0nope";
	const char* ids[] = {"TXXX", "TXXX"};
	const uint8_t* bodies[] = {miss, hit};
	int lens[] = {sizeof(miss) - 1, sizeof(hit) - 1};
	uint8_t tag[128];
	int n = build_tag(tag, ids, bodies, lens, 2);
	char artist[64], title[64];
	radio_hls_parse_id3_metadata(tag, n, artist, sizeof(artist), title, sizeof(title));
	CHECK(strcmp(artist, "X") == 0);
	CHECK(strcmp(title, "Y") == 0);
}

static void test_priv_latin1_stream_title(void) {
	// Not valid UTF-8, so it is read as Latin-1
	const uint8_t priv[] = "StreamTitle='Beyonc\xE9 - Halo';";
	const char* ids[] = {"PRIV"};
	const uint8_t* bodies[] = {priv};
	int lens[] = {sizeof(priv) - 1};
	uint8_t tag[128];
	int n = build_tag(tag, ids, bodies, lens, 1);
	char artist[64] = "", title[64] = "";
	radio_hls_parse_id3_metadata(tag, n, artist, sizeof(artist), title, sizeof(title));
	CHECK(strcmp(artist, "Beyonc\xC3\xA9") == 0);
	CHECK(strcmp(title, "Halo") == 0);
}

int main(void) {
	test_utf16_title_artist();
	test_latin1_title();
	test_txxx_utf16_stream_title();
	test_txxx_utf8_and_other_description();
	test_priv_latin1_stream_title();
	if (failures) {
		fprintf(stderr, "test_radio_hls_id3: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_radio_hls_id3: all tests passed\n");
	return 0;
}
