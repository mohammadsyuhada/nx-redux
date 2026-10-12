// Host test for embedded_lyrics.c and tag_meta.c: builds small synthetic
// ID3 / FLAC / Ogg / MP4 files and checks the lyrics and tag fields read back.
#include "embedded_lyrics.h"
#include "tag_meta.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond)                                                         \
	do {                                                                    \
		if (!(cond)) {                                                      \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                     \
		}                                                                   \
	} while (0)

#define TMP_PATH "/tmp/nx_test_embedded_lyrics.bin"

typedef struct {
	uint8_t* p;
	size_t len;
	size_t cap;
} Buf;

static void put(Buf* b, const void* data, size_t n) {
	if (b->len + n > b->cap) {
		b->cap = (b->len + n) * 2 + 64;
		b->p = realloc(b->p, b->cap);
	}
	memcpy(b->p + b->len, data, n);
	b->len += n;
}
static void put8(Buf* b, uint8_t v) {
	put(b, &v, 1);
}
static void put_str(Buf* b, const char* s) {
	put(b, s, strlen(s));
}
static void put_be32(Buf* b, uint32_t v) {
	uint8_t x[4] = {v >> 24, v >> 16, v >> 8, v};
	put(b, x, 4);
}
static void put_le32(Buf* b, uint32_t v) {
	uint8_t x[4] = {v, v >> 8, v >> 16, v >> 24};
	put(b, x, 4);
}
static void put_syncsafe(Buf* b, uint32_t v) {
	uint8_t x[4] = {(v >> 21) & 0x7F, (v >> 14) & 0x7F, (v >> 7) & 0x7F, v & 0x7F};
	put(b, x, 4);
}
// UTF-16LE with BOM, no terminator
static void put_utf16(Buf* b, const char* ascii) {
	put8(b, 0xFF);
	put8(b, 0xFE);
	for (; *ascii; ascii++) {
		put8(b, (uint8_t)*ascii);
		put8(b, 0);
	}
}

static void write_file(const Buf* b) {
	FILE* f = fopen(TMP_PATH, "wb");
	fwrite(b->p, 1, b->len, f);
	fclose(f);
}

static char* read_back(const Buf* b, bool* synced) {
	write_file(b);
	return EmbeddedLyrics_read(TMP_PATH, synced);
}

// ID3v2.<ver> tag holding the given frames, followed by fake audio
static void id3_tag(Buf* out, int ver, const Buf* frames) {
	put_str(out, "ID3");
	put8(out, (uint8_t)ver);
	put8(out, 0);
	put8(out, 0);
	put_syncsafe(out, (uint32_t)frames->len + 32); // + padding
	put(out, frames->p, frames->len);
	for (int i = 0; i < 32; i++)
		put8(out, 0);
	put_str(out, "\xFF\xFB audio");
}

static void id3_frame(Buf* frames, int ver, const char* id, const Buf* body) {
	put_str(frames, id);
	if (ver == 4)
		put_syncsafe(frames, (uint32_t)body->len);
	else
		put_be32(frames, (uint32_t)body->len);
	put8(frames, 0);
	put8(frames, 0);
	put(frames, body->p, body->len);
}

static void test_id3_uslt_utf16(void) {
	Buf body = {0}, frames = {0}, file = {0};
	put8(&body, 1); // UTF-16 with BOM
	put_str(&body, "eng");
	put_utf16(&body, "desc");
	put8(&body, 0);
	put8(&body, 0);
	put_utf16(&body, "Line one\r\nLine two");
	id3_frame(&frames, 3, "USLT", &body);
	id3_tag(&file, 3, &frames);

	bool synced = true;
	char* text = read_back(&file, &synced);
	CHECK(text && strcmp(text, "Line one\nLine two") == 0);
	CHECK(!synced);
	free(text);
	free(body.p);
	free(frames.p);
	free(file.p);
}

static void test_id3_uslt_latin1_lrc(void) {
	Buf body = {0}, frames = {0}, file = {0};
	put8(&body, 0); // Latin-1
	put_str(&body, "eng");
	put8(&body, 0); // empty descriptor
	put_str(&body, "[00:01.00]Caf\xE9\n[00:02.50]Two");
	id3_frame(&frames, 4, "USLT", &body);
	id3_tag(&file, 4, &frames);

	bool synced = false;
	char* text = read_back(&file, &synced);
	CHECK(text && strcmp(text, "[00:01.00]Caf\xC3\xA9\n[00:02.50]Two") == 0); // é as UTF-8
	CHECK(synced);
	free(text);
	free(body.p);
	free(frames.p);
	free(file.p);
}

static void sylt_body(Buf* body, uint8_t ts_format, const char** texts, const uint32_t* ms, int n) {
	put8(body, 3); // UTF-8
	put_str(body, "eng");
	put8(body, ts_format);
	put8(body, 1); // content type: lyrics
	put8(body, 0); // empty descriptor
	for (int i = 0; i < n; i++) {
		put_str(body, texts[i]);
		put8(body, 0);
		put_be32(body, ms[i]);
	}
}

static void test_id3_sylt_beats_uslt(void) {
	Buf uslt = {0}, sylt = {0}, frames = {0}, file = {0};
	put8(&uslt, 3);
	put_str(&uslt, "eng");
	put8(&uslt, 0);
	put_str(&uslt, "plain words");
	const char* texts[] = {"First", "Second"};
	const uint32_t ms[] = {1500, 65020};
	sylt_body(&sylt, 2, texts, ms, 2);
	id3_frame(&frames, 3, "USLT", &uslt);
	id3_frame(&frames, 3, "SYLT", &sylt);
	id3_tag(&file, 3, &frames);

	bool synced = false;
	char* text = read_back(&file, &synced);
	CHECK(text && strcmp(text, "[00:01.50]First\n[01:05.02]Second") == 0);
	CHECK(synced);
	free(text);
	free(uslt.p);
	free(sylt.p);
	free(frames.p);
	free(file.p);
}

static void test_id3_sylt_karaoke_joined(void) {
	Buf sylt = {0}, frames = {0}, file = {0};
	const char* texts[] = {"Hel", "lo", "\nWor", "ld"};
	const uint32_t ms[] = {1000, 1200, 3000, 3300};
	sylt_body(&sylt, 2, texts, ms, 4);
	id3_frame(&frames, 4, "SYLT", &sylt);
	id3_tag(&file, 4, &frames);

	bool synced = false;
	char* text = read_back(&file, &synced);
	CHECK(text && strcmp(text, "[00:01.00]Hello\n[00:03.00]World") == 0);
	CHECK(synced);
	free(text);
	free(sylt.p);
	free(frames.p);
	free(file.p);
}

static void test_id3_sylt_mpeg_frames_skipped(void) {
	Buf sylt = {0}, frames = {0}, file = {0};
	const char* texts[] = {"Frame timed"};
	const uint32_t ms[] = {10};
	sylt_body(&sylt, 1, texts, ms, 1);
	id3_frame(&frames, 3, "SYLT", &sylt);
	id3_tag(&file, 3, &frames);

	bool synced = true;
	char* text = read_back(&file, &synced);
	CHECK(text == NULL);
	CHECK(!synced);
	free(sylt.p);
	free(frames.p);
	free(file.p);
}

static void vorbis_comment_block(Buf* b, const char** comments, int n) {
	put_le32(b, 6);
	put_str(b, "vendor");
	put_le32(b, (uint32_t)n);
	for (int i = 0; i < n; i++) {
		put_le32(b, (uint32_t)strlen(comments[i]));
		put_str(b, comments[i]);
	}
}

static void flac_file(Buf* file, const char** comments, int n) {
	put_str(file, "fLaC");
	// STREAMINFO (34 zero bytes are fine for the lyrics reader)
	put8(file, 0);
	put8(file, 0);
	put8(file, 0);
	put8(file, 34);
	for (int i = 0; i < 34; i++)
		put8(file, 0);
	Buf vc = {0};
	vorbis_comment_block(&vc, comments, n);
	put8(file, 0x80 | 4); // last block, VORBIS_COMMENT
	put8(file, (uint8_t)(vc.len >> 16));
	put8(file, (uint8_t)(vc.len >> 8));
	put8(file, (uint8_t)vc.len);
	put(file, vc.p, vc.len);
	free(vc.p);
	put_str(file, "\xFF\xF8 frames");
}

static void test_flac_lyrics(void) {
	Buf file = {0};
	const char* comments[] = {"TITLE=Song", "UNSYNCEDLYRICS=plain one\nplain two",
							  "lyrics=[00:10.00]timed"};
	flac_file(&file, comments, 3);
	bool synced = false;
	char* text = read_back(&file, &synced);
	CHECK(text && strcmp(text, "[00:10.00]timed") == 0); // timed wins over plain
	CHECK(synced);
	free(text);
	free(file.p);

	Buf plain = {0};
	const char* plain_comments[] = {"LYRICS=  \n just words \n"};
	flac_file(&plain, plain_comments, 1);
	text = read_back(&plain, &synced);
	CHECK(text && strcmp(text, "just words") == 0);
	CHECK(!synced);
	free(text);
	free(plain.p);

	Buf none = {0};
	const char* no_lyrics[] = {"TITLE=Song", "ARTIST=Band"};
	flac_file(&none, no_lyrics, 2);
	text = read_back(&none, &synced);
	CHECK(text == NULL);
	free(none.p);
}

static void test_flac_behind_id3(void) {
	Buf flac = {0}, file = {0}, frames = {0}, body = {0};
	const char* comments[] = {"LYRICS=[00:01.00]after id3"};
	flac_file(&flac, comments, 1);
	// An ID3 tag with only a text frame, then the FLAC stream
	put8(&body, 3);
	put_str(&body, "Title");
	id3_frame(&frames, 3, "TIT2", &body);
	put_str(&file, "ID3");
	put8(&file, 3);
	put8(&file, 0);
	put8(&file, 0);
	put_syncsafe(&file, (uint32_t)frames.len);
	put(&file, frames.p, frames.len);
	put(&file, flac.p, flac.len);

	bool synced = false;
	char* text = read_back(&file, &synced);
	CHECK(text && strcmp(text, "[00:01.00]after id3") == 0);
	CHECK(synced);
	free(text);
	free(flac.p);
	free(file.p);
	free(frames.p);
	free(body.p);
}

// Ogg page with the given lacing values and body
static void ogg_page(Buf* out, uint32_t serial, uint32_t seq, const uint8_t* lacing, int nsegs,
					 const uint8_t* body, size_t body_len) {
	put_str(out, "OggS");
	put8(out, 0);
	put8(out, 0);
	for (int i = 0; i < 8; i++)
		put8(out, 0);
	put_le32(out, serial);
	put_le32(out, seq);
	put_le32(out, 0); // CRC (not checked by the reader)
	put8(out, (uint8_t)nsegs);
	put(out, lacing, (size_t)nsegs);
	put(out, body, body_len);
}

// Split a packet over pages of at most max_segs segments each
static void ogg_packet(Buf* out, uint32_t serial, uint32_t* seq, const Buf* packet, int max_segs) {
	size_t off = 0;
	bool done = false;
	while (!done) {
		uint8_t lacing[255];
		int nsegs = 0;
		size_t page_start = off;
		while (nsegs < max_segs) {
			size_t left = packet->len - off;
			if (left >= 255) {
				lacing[nsegs++] = 255;
				off += 255;
			} else {
				lacing[nsegs++] = (uint8_t)left;
				off += left;
				done = true;
				break;
			}
		}
		ogg_page(out, serial, (*seq)++, lacing, nsegs, packet->p + page_start, off - page_start);
	}
}

static void ogg_file(Buf* file, bool opus, const char** comments, int n, size_t pad_comment) {
	uint32_t seq = 0;
	Buf head = {0}, tags = {0};
	if (opus) {
		put_str(&head, "OpusHead");
		for (int i = 0; i < 11; i++)
			put8(&head, 1);
		put_str(&tags, "OpusTags");
	} else {
		put_str(&head, "\x01vorbis");
		for (int i = 0; i < 23; i++)
			put8(&head, 1);
		put_str(&tags, "\x03vorbis");
	}
	// A big leading comment (like a cover) forces the packet across pages
	Buf vc = {0};
	put_le32(&vc, 3);
	put_str(&vc, "enc");
	put_le32(&vc, (uint32_t)n + (pad_comment ? 1 : 0));
	if (pad_comment) {
		put_le32(&vc, (uint32_t)(pad_comment + 23));
		put_str(&vc, "METADATA_BLOCK_PICTURE=");
		for (size_t i = 0; i < pad_comment; i++)
			put8(&vc, 'A');
	}
	for (int i = 0; i < n; i++) {
		put_le32(&vc, (uint32_t)strlen(comments[i]));
		put_str(&vc, comments[i]);
	}
	put(&tags, vc.p, vc.len);
	// An unrelated logical stream page first must be skipped after the
	// first page fixes the serial
	ogg_packet(file, 7, &seq, &head, 255);
	uint8_t lacing = 3;
	ogg_page(file, 99, 0, &lacing, 1, (const uint8_t*)"xyz", 3);
	ogg_packet(file, 7, &seq, &tags, 4);
	put_str(file, "OggS audio");
	free(head.p);
	free(tags.p);
	free(vc.p);
}

static void test_ogg_lyrics(void) {
	const char* comments[] = {"TITLE=T", "LYRICS=[00:05.00]ogg timed"};
	for (int opus = 0; opus <= 1; opus++) {
		Buf file = {0};
		ogg_file(&file, opus, comments, 2, 5000); // ~5 pages of 4 segments
		bool synced = false;
		char* text = read_back(&file, &synced);
		CHECK(text && strcmp(text, "[00:05.00]ogg timed") == 0);
		CHECK(synced);
		free(text);
		free(file.p);
	}

	Buf small = {0};
	const char* plain[] = {"UNSYNCED LYRICS=words"};
	ogg_file(&small, false, plain, 1, 0);
	bool synced = true;
	char* text = read_back(&small, &synced);
	CHECK(text && strcmp(text, "words") == 0);
	CHECK(!synced);
	free(text);
	free(small.p);
}

static void mp4_box(Buf* out, const char* type, const Buf* payload) {
	put_be32(out, (uint32_t)(8 + payload->len));
	put(out, type, 4);
	put(out, payload->p, payload->len);
}

static void test_mp4_lyrics(void) {
	Buf data = {0}, lyr = {0}, ilst = {0}, meta = {0}, udta = {0}, moov = {0}, file = {0}, ftyp = {0}, mdat = {0};
	put_be32(&data, 1); // UTF-8
	put_be32(&data, 0);
	put_str(&data, "[00:02.00]mp4 line");
	mp4_box(&lyr, "data", &data);
	mp4_box(&ilst, "\xa9lyr", &lyr);
	put_be32(&meta, 0); // full box version/flags
	Buf hdlr = {0};
	put_str(&hdlr, "\0\0\0\0mdirappl\0\0\0\0\0\0\0\0\0");
	mp4_box(&meta, "hdlr", &hdlr);
	mp4_box(&meta, "ilst", &ilst);
	mp4_box(&udta, "meta", &meta);
	Buf mvhd = {0};
	put_str(&mvhd, "0123");
	mp4_box(&moov, "mvhd", &mvhd);
	mp4_box(&moov, "udta", &udta);
	put_str(&ftyp, "M4A \0\0\0\0");
	mp4_box(&file, "ftyp", &ftyp);
	put_str(&mdat, "audio");
	mp4_box(&file, "mdat", &mdat);
	mp4_box(&file, "moov", &moov); // moov after mdat, like many encoders

	bool synced = false;
	char* text = read_back(&file, &synced);
	CHECK(text && strcmp(text, "[00:02.00]mp4 line") == 0);
	CHECK(synced);
	free(text);
	free(data.p);
	free(lyr.p);
	free(ilst.p);
	free(meta.p);
	free(udta.p);
	free(moov.p);
	free(file.p);
	free(ftyp.p);
	free(mdat.p);
	free(hdlr.p);
	free(mvhd.p);
}

static void test_missing_and_unknown(void) {
	bool synced = true;
	CHECK(EmbeddedLyrics_read("/nonexistent/file.flac", &synced) == NULL);
	CHECK(!synced);
	CHECK(EmbeddedLyrics_read(NULL, &synced) == NULL);
	Buf wav = {0};
	put_str(&wav, "RIFF\0\0\0\0WAVEfmt ");
	char* text = read_back(&wav, &synced);
	CHECK(text == NULL);
	free(wav.p);
}

static void test_is_lrc(void) {
	CHECK(EmbeddedLyrics_isLrc("[ar:Band]\n[00:01.00]x"));
	CHECK(EmbeddedLyrics_isLrc("  [1:02]x"));
	CHECK(!EmbeddedLyrics_isLrc("[Chorus]\nla la"));
	CHECK(!EmbeddedLyrics_isLrc("plain"));
	CHECK(!EmbeddedLyrics_isLrc(""));
}

static void test_vorbis_fields(void) {
	const char* v;
	size_t n;
	const char* c = "title=  Hello World ";
	CHECK(TagMeta_vorbisField(c, strlen(c), &v, &n) == TAG_FIELD_TITLE);
	CHECK(n == 11 && strncmp(v, "Hello World", n) == 0);
	c = "ARTIST=";
	CHECK(TagMeta_vorbisField(c, strlen(c), &v, &n) == TAG_FIELD_NONE); // empty value
	c = "ALBUM=   ";
	CHECK(TagMeta_vorbisField(c, strlen(c), &v, &n) == TAG_FIELD_NONE);
	c = "Album Artist=Various";
	CHECK(TagMeta_vorbisField(c, strlen(c), &v, &n) == TAG_FIELD_ALBUM_ARTIST);
	c = "ALBUMARTIST=Various";
	CHECK(TagMeta_vorbisField(c, strlen(c), &v, &n) == TAG_FIELD_ALBUM_ARTIST);
	c = "ALBUM=Record";
	CHECK(TagMeta_vorbisField(c, strlen(c), &v, &n) == TAG_FIELD_ALBUM);
	c = "ALBUMX=Record";
	CHECK(TagMeta_vorbisField(c, strlen(c), &v, &n) == TAG_FIELD_NONE);
	c = "GENRE=Rock";
	CHECK(TagMeta_vorbisField(c, strlen(c), &v, &n) == TAG_FIELD_NONE);
	c = "no equals";
	CHECK(TagMeta_vorbisField(c, strlen(c), &v, &n) == TAG_FIELD_NONE);
	// Not NUL-terminated at len: only the first len bytes count
	c = "TITLE=AbcTRAILING";
	CHECK(TagMeta_vorbisField(c, 9, &v, &n) == TAG_FIELD_TITLE && n == 3 && strncmp(v, "Abc", 3) == 0);
}

static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char* base64(const uint8_t* p, size_t n) {
	char* out = malloc(n / 3 * 4 + 5);
	size_t o = 0;
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
		out[o++] = b64chars[(v >> 18) & 63];
		out[o++] = b64chars[(v >> 12) & 63];
		out[o++] = i + 1 < n ? b64chars[(v >> 6) & 63] : '=';
		out[o++] = i + 2 < n ? b64chars[v & 63] : '=';
	}
	out[o] = '\0';
	return out;
}

static void test_block_picture(void) {
	Buf block = {0};
	put_be32(&block, 3);
	put_be32(&block, 10);
	put_str(&block, "image/jpeg");
	put_be32(&block, 5);
	put_str(&block, "cover");
	for (int i = 0; i < 16; i++)
		put8(&block, 0);
	put_be32(&block, 4);
	put_str(&block, "\xFF\xD8\xFF\xE0");
	char* b64 = base64(block.p, block.len);

	const uint8_t* image;
	size_t size;
	uint32_t type;
	uint8_t* owned = TagMeta_decodeBlockPicture(b64, strlen(b64), &image, &size, &type);
	CHECK(owned != NULL);
	CHECK(type == 3 && size == 4 && memcmp(image, "\xFF\xD8\xFF\xE0", 4) == 0);
	free(owned);

	// Truncated block: data length claims more than present
	char* cut = base64(block.p, block.len - 2);
	CHECK(TagMeta_decodeBlockPicture(cut, strlen(cut), &image, &size, &type) == NULL);
	CHECK(TagMeta_decodeBlockPicture("!!notbase64", 11, &image, &size, &type) == NULL);
	CHECK(TagMeta_decodeBlockPicture("", 0, &image, &size, &type) == NULL);
	free(b64);
	free(cut);
	free(block.p);
}

static void test_id3_text(void) {
	char out[64];
	// UTF-16LE with BOM: 周杰伦 (the frame that used to come back empty)
	const uint8_t le[] = {0xFF, 0xFE, 0x68, 0x54, 0x70, 0x67, 0x26, 0x4F};
	TagMeta_id3Text(1, le, sizeof(le), out, sizeof(out));
	CHECK(strcmp(out, "\xE5\x91\xA8\xE6\x9D\xB0\xE4\xBC\xA6") == 0);
	// UTF-16 with a big-endian BOM, terminator, then junk
	const uint8_t be[] = {0xFE, 0xFF, 0x00, 'A', 0x00, 0xE9, 0x00, 0x00, 0x00, 'X'};
	TagMeta_id3Text(1, be, sizeof(be), out, sizeof(out));
	CHECK(strcmp(out, "A\xC3\xA9") == 0);
	// UTF-16BE without BOM, surrogate pair U+1F600
	const uint8_t be2[] = {0xD8, 0x3D, 0xDE, 0x00};
	TagMeta_id3Text(2, be2, sizeof(be2), out, sizeof(out));
	CHECK(strcmp(out, "\xF0\x9F\x98\x80") == 0);
	// Latin-1 is converted, not copied raw
	const uint8_t l1[] = {'C', 0xE9, 'l', 'i', 'n', 'e'};
	TagMeta_id3Text(0, l1, sizeof(l1), out, sizeof(out));
	CHECK(strcmp(out, "C\xC3\xA9line") == 0);
	// UTF-8 passes through
	const char* u8 = "\xE5\x8F\x8C\xE6\x88\xAA";
	TagMeta_id3Text(3, (const uint8_t*)u8, strlen(u8), out, sizeof(out));
	CHECK(strcmp(out, u8) == 0);
	// Truncation never splits a character: 3-byte chars into a 6-byte buffer
	TagMeta_id3Text(1, le, sizeof(le), out, 6);
	CHECK(strcmp(out, "\xE5\x91\xA8") == 0);
	TagMeta_id3Text(3, (const uint8_t*)u8, strlen(u8), out, 6);
	CHECK(strcmp(out, "\xE5\x8F\x8C") == 0);
	// Unpaired surrogate becomes U+FFFD
	const uint8_t lone[] = {0x00, 0xD8, 'a', 0x00};
	TagMeta_id3Text(1, lone, sizeof(lone), out, sizeof(out));
	CHECK(strcmp(out, "\xEF\xBF\xBD"
					  "a") == 0);
	// Empty / odd input
	TagMeta_id3Text(1, le, 1, out, sizeof(out));
	CHECK(out[0] == '\0');
}

static void test_id3_picture(void) {
	const uint8_t* image;
	size_t image_size;
	uint32_t type;
	// UTF-16 description ending in an ASCII char ("YA.jpg"): its terminator
	// starts on an even offset, after the 0x00 high byte of 'g' (issue #213)
	const uint8_t u16[] = {1, 'i', 'm', 'a', 'g', 'e', '/', 'j', 'p', 'e', 'g', 0, 3,
						   0xFF, 0xFE, 'Y', 0, 'A', 0, '.', 0, 'j', 0, 'p', 0, 'g',
						   0, 0, 0, 0x89, 'P', 'N', 'G'};
	CHECK(TagMeta_id3Picture(u16, sizeof(u16), &image, &image_size, &type));
	CHECK(type == TAG_PICTURE_FRONT_COVER);
	CHECK(image_size == 4 && memcmp(image, "\x89PNG", 4) == 0);
	// UTF-16BE description
	const uint8_t be[] = {2, 'i', 0, 4, 0, 'A', 0, 'B', 0, 0, 0xFF, 0xD8};
	CHECK(TagMeta_id3Picture(be, sizeof(be), &image, &image_size, &type));
	CHECK(type == 4 && image_size == 2 && image[0] == 0xFF && image[1] == 0xD8);
	// Empty UTF-16 description
	const uint8_t empty16[] = {1, 0, 3, 0, 0, 0xFF, 0xD8};
	CHECK(TagMeta_id3Picture(empty16, sizeof(empty16), &image, &image_size, &type));
	CHECK(image_size == 2 && image[0] == 0xFF);
	// Latin-1 description
	const uint8_t l1[] = {0, 'i', 0, 3, 'c', 'o', 'v', 0, 0xFF, 0xD8, 0xFF};
	CHECK(TagMeta_id3Picture(l1, sizeof(l1), &image, &image_size, &type));
	CHECK(image_size == 3 && image[0] == 0xFF);
	// Truncated frames: no terminator, or nothing after it
	const uint8_t cut[] = {1, 'i', 0, 3, 0xFF, 0xFE, 'A', 0};
	CHECK(!TagMeta_id3Picture(cut, sizeof(cut), &image, &image_size, &type));
	const uint8_t bare[] = {0, 'i', 0, 3, 0};
	CHECK(!TagMeta_id3Picture(bare, sizeof(bare), &image, &image_size, &type));
	CHECK(!TagMeta_id3Picture(u16, 1, &image, &image_size, &type));
}

static void test_stream_text(void) {
	char out[64];
	// Valid UTF-8 is kept
	const char* u8 = "\xE5\x91\xA8\xE6\x9D\xB0\xE4\xBC\xA6 - Caf\xC3\xA9";
	TagMeta_streamText(u8, strlen(u8), out, sizeof(out));
	CHECK(strcmp(out, u8) == 0);
	// Invalid UTF-8 is read as Latin-1
	const char* l1 = "Caf\xE9 Ol\xE9";
	TagMeta_streamText(l1, strlen(l1), out, sizeof(out));
	CHECK(strcmp(out, "Caf\xC3\xA9 Ol\xC3\xA9") == 0);
	// A sequence cut off by len is not valid UTF-8
	TagMeta_streamText(u8, 2, out, sizeof(out));
	CHECK(strcmp(out, "\xC3\xA5\xC2\x91") == 0);
	// Overlong encoding is rejected
	const char* overlong = "\xC0\xAF";
	TagMeta_streamText(overlong, 2, out, sizeof(out));
	CHECK(strcmp(out, "\xC3\x80\xC2\xAF") == 0);
	// Stops at NUL within len
	TagMeta_streamText("ab\0cd", 5, out, sizeof(out));
	CHECK(strcmp(out, "ab") == 0);
}

int main(void) {
	test_id3_uslt_utf16();
	test_id3_uslt_latin1_lrc();
	test_id3_sylt_beats_uslt();
	test_id3_sylt_karaoke_joined();
	test_id3_sylt_mpeg_frames_skipped();
	test_flac_lyrics();
	test_flac_behind_id3();
	test_ogg_lyrics();
	test_mp4_lyrics();
	test_missing_and_unknown();
	test_is_lrc();
	test_vorbis_fields();
	test_block_picture();
	test_id3_text();
	test_stream_text();
	test_id3_picture();
	remove(TMP_PATH);
	if (failures) {
		fprintf(stderr, "test_embedded_lyrics: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_embedded_lyrics: all tests passed\n");
	return 0;
}
