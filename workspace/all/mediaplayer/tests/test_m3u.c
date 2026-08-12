// Host-compiled unit test for m3u.c (no device toolchain).
// Build & run:
//   cc -I. m3u.c tests/test_m3u.c -o /tmp/test_m3u && /tmp/test_m3u
#include <assert.h>
#include <stdio.h>
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
	CuratedTVChannel ch[8];

	// Well-formed: two channels, attributes present, CRLF on one line.
	write_file(path,
			   "#EXTM3U\n"
			   "#EXTINF:-1 tvg-id=\"A.us\" tvg-logo=\"http://x/a.png\" group-title=\"News\",Alpha (1080p)\r\n"
			   "http://host/a/index.m3u8\n"
			   "#EXTINF:-1 group-title=\"Sports\",Bravo\n"
			   "http://host/b/index.m3u8\n");
	int n = M3U_parseFile(path, ch, 8, "us");
	assert(n == 2);
	assert(strcmp(ch[0].name, "Alpha (1080p)") == 0);
	assert(strcmp(ch[0].url, "http://host/a/index.m3u8") == 0);
	assert(strcmp(ch[0].logo, "http://x/a.png") == 0);
	assert(strcmp(ch[0].category, "News") == 0);
	assert(strcmp(ch[0].country_code, "us") == 0);
	assert(strcmp(ch[1].name, "Bravo") == 0);
	assert(ch[1].logo[0] == '\0'); // missing tvg-logo -> empty
	assert(strcmp(ch[1].url, "http://host/b/index.m3u8") == 0);

	// #EXTINF with no following URL is dropped; blank lines + stray comments ignored.
	write_file(path,
			   "#EXTM3U\n"
			   "#EXTINF:-1,Orphan\n"
			   "\n"
			   "#EXTVLCOPT:foo=bar\n"
			   "#EXTINF:-1,Real\n"
			   "http://host/real\n");
	n = M3U_parseFile(path, ch, 8, NULL);
	assert(n == 1);
	assert(strcmp(ch[0].name, "Real") == 0);
	assert(strcmp(ch[0].url, "http://host/real") == 0);

	// max cap respected.
	write_file(path,
			   "#EXTINF:-1,One\nhttp://1\n#EXTINF:-1,Two\nhttp://2\n#EXTINF:-1,Three\nhttp://3\n");
	n = M3U_parseFile(path, ch, 2, NULL);
	assert(n == 2);

	// Missing file -> -1.
	assert(M3U_parseFile("/tmp/does-not-exist.m3u", ch, 8, NULL) == -1);

	printf("test_m3u: OK\n");
	return 0;
}
