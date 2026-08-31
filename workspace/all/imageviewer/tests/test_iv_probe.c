// Host-compiled unit test for iv_probe.c.
// Build & run (from workspace/all/imageviewer):
//   cc -std=gnu99 iv_probe.c tests/test_iv_probe.c -o /tmp/test_iv_probe && /tmp/test_iv_probe
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../iv_probe.h"

static void write_file(const char* path, const unsigned char* buf, int len) {
	FILE* f = fopen(path, "wb");
	fwrite(buf, 1, len, f);
	fclose(f);
}

int main(void) {
	int w = -1, h = -1;

	// PNG: 8-byte signature, IHDR length+type, then 4-byte BE width=1024 height=768
	const unsigned char png[] = {
		0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
		0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R',
		0x00, 0x00, 0x04, 0x00, /* w=1024 */
		0x00, 0x00, 0x03, 0x00, /* h=768 */
		0x08, 0x06, 0x00, 0x00, 0x00};
	write_file("/tmp/iv-probe.png", png, sizeof(png));
	assert(IvProbe_dimensions("/tmp/iv-probe.png", &w, &h) && w == 1024 && h == 768);

	// JPEG: SOI, APP0 (16 bytes), SOF0 with h=480 w=640
	const unsigned char jpg[] = {
		0xFF, 0xD8,
		0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00,
		0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
		0xFF, 0xC0, 0x00, 0x11, 0x08,
		0x01, 0xE0, /* height 480 */
		0x02, 0x80, /* width 640 */
		0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01};
	write_file("/tmp/iv-probe.jpg", jpg, sizeof(jpg));
	assert(IvProbe_dimensions("/tmp/iv-probe.jpg", &w, &h) && w == 640 && h == 480);

	// GIF: "GIF89a" + LE width=320 height=200
	const unsigned char gif[] = {'G', 'I', 'F', '8', '9', 'a', 0x40, 0x01, 0xC8, 0x00, 0x00, 0x00, 0x00};
	write_file("/tmp/iv-probe.gif", gif, sizeof(gif));
	assert(IvProbe_dimensions("/tmp/iv-probe.gif", &w, &h) && w == 320 && h == 200);

	// BMP: "BM", 40-byte DIB, LE int32 width=800 at 18, height=600 at 22
	unsigned char bmp[54];
	memset(bmp, 0, sizeof(bmp));
	bmp[0] = 'B';
	bmp[1] = 'M';
	bmp[14] = 40;
	bmp[18] = 0x20;
	bmp[19] = 0x03; /* 800 */
	bmp[22] = 0x58;
	bmp[23] = 0x02; /* 600 */
	write_file("/tmp/iv-probe.bmp", bmp, sizeof(bmp));
	assert(IvProbe_dimensions("/tmp/iv-probe.bmp", &w, &h) && w == 800 && h == 600);

	// Garbage: must return false
	const unsigned char junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	write_file("/tmp/iv-probe.bin", junk, sizeof(junk));
	assert(!IvProbe_dimensions("/tmp/iv-probe.bin", &w, &h));
	// Missing file: false
	assert(!IvProbe_dimensions("/tmp/iv-probe-missing.png", &w, &h));

	printf("test_iv_probe: all assertions passed\n");
	return 0;
}
