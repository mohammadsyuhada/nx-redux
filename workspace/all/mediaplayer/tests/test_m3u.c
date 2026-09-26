// Host-compiled unit test for m3u.c (no device toolchain).
// Build & run:
//   cc -I. m3u.c tests/test_m3u.c -o /tmp/test_m3u && /tmp/test_m3u
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../m3u.h"

static void write_file(const char* path, const char* content) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(content, f);
	fclose(f);
}

int main(void) {
	const char* path = "/tmp/test.m3u";
	CuratedTVChannel* ch = NULL;

	// Well-formed: two channels, attributes present, CRLF on one line.
	write_file(path,
			   "#EXTM3U\n"
			   "#EXTINF:-1 tvg-id=\"A.us\" tvg-logo=\"http://x/a.png\" group-title=\"News\",Alpha (1080p)\r\n"
			   "http://host/a/index.m3u8\n"
			   "#EXTINF:-1 group-title=\"Sports\",Bravo\n"
			   "http://host/b/index.m3u8\n");
	int n = M3U_parseFile(path, &ch, "us");
	assert(n == 2);
	assert(ch != NULL);
	assert(strcmp(ch[0].name, "Alpha (1080p)") == 0);
	assert(strcmp(ch[0].url, "http://host/a/index.m3u8") == 0);
	assert(strcmp(ch[0].logo, "http://x/a.png") == 0);
	assert(strcmp(ch[0].category, "News") == 0);
	assert(strcmp(ch[0].country_code, "us") == 0);
	assert(strcmp(ch[1].name, "Bravo") == 0);
	assert(ch[1].logo[0] == '\0'); // missing tvg-logo -> empty
	assert(strcmp(ch[1].url, "http://host/b/index.m3u8") == 0);
	free(ch);
	ch = NULL;

	// #EXTINF with no following URL is dropped; blank lines + stray comments ignored.
	write_file(path,
			   "#EXTM3U\n"
			   "#EXTINF:-1,Orphan\n"
			   "\n"
			   "#EXTVLCOPT:foo=bar\n"
			   "#EXTINF:-1,Real\n"
			   "http://host/real\n");
	n = M3U_parseFile(path, &ch, NULL);
	assert(n == 1);
	assert(ch != NULL);
	assert(strcmp(ch[0].name, "Real") == 0);
	assert(strcmp(ch[0].url, "http://host/real") == 0);
	free(ch);
	ch = NULL;

	// No artificial cap: a list far larger than the old 512/256 limits parses
	// in full (the regression this test guards against).
	{
		const int channel_count = 2000;
		size_t cap = 128000;
		char* big = malloc(cap);
		assert(big);
		size_t pos = 0;
		for (int i = 0; i < channel_count; i++) {
			int w = snprintf(big + pos, cap - pos, "#EXTINF:-1,Ch%d\nhttp://host/%d\n", i, i);
			assert(w > 0 && (size_t)w < cap - pos);
			pos += (size_t)w;
		}
		write_file(path, big);
		free(big);
		n = M3U_parseFile(path, &ch, NULL);
		assert(n == channel_count);
		assert(ch != NULL);
		assert(strcmp(ch[0].name, "Ch0") == 0);
		assert(strcmp(ch[channel_count - 1].name, "Ch1999") == 0);
		free(ch);
		ch = NULL;
	}

	// No channels at all -> count 0, *out left NULL.
	write_file(path, "#EXTM3U\n");
	n = M3U_parseFile(path, &ch, NULL);
	assert(n == 0);
	assert(ch == NULL);

	// Missing file -> -1, *out left NULL.
	n = M3U_parseFile("/tmp/does-not-exist.m3u", &ch, NULL);
	assert(n == -1);
	assert(ch == NULL);

	printf("test_m3u: OK\n");
	return 0;
}
