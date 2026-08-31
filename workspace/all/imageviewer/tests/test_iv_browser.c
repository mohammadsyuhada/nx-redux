// Host-compiled unit test for iv_browser.c (no device toolchain needed).
// Build & run (from workspace/all/imageviewer):
//   cc -std=gnu99 iv_browser.c tests/test_iv_browser.c -o /tmp/test_iv_browser && /tmp/test_iv_browser
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../iv_browser.h"

int main(void) {
	system("rm -rf /tmp/iv-test && mkdir -p /tmp/iv-test/Screenshots /tmp/iv-test/Empty");
	system("printf x > /tmp/iv-test/photo.JPG");
	system("printf x > /tmp/iv-test/art.png");
	system("printf x > /tmp/iv-test/notes.txt");   // filtered out
	system("printf x > /tmp/iv-test/.hidden.png"); // hidden: filtered out
	system("printf x > /tmp/iv-test/Screenshots/a.jpeg");

	// Extension filter
	assert(ImageBrowser_isImageFile("a.png"));
	assert(ImageBrowser_isImageFile("B.JPG"));
	assert(ImageBrowser_isImageFile("c.jpeg"));
	assert(ImageBrowser_isImageFile("d.bmp"));
	assert(ImageBrowser_isImageFile("e.gif"));
	assert(!ImageBrowser_isImageFile("f.txt"));
	assert(!ImageBrowser_isImageFile("noext"));

	// Root load: dirs first (Empty, Screenshots), then files sorted (art.png, photo.JPG)
	ImageBrowserContext ctx;
	memset(&ctx, 0, sizeof(ctx));
	ImageBrowser_loadDirectory(&ctx, "/tmp/iv-test", "/tmp/iv-test");
	assert(ctx.entry_count == 4); // no ".." at root
	assert(ctx.entries[0].is_dir && strcmp(ctx.entries[0].name, "Empty") == 0);
	assert(ctx.entries[1].is_dir && strcmp(ctx.entries[1].name, "Screenshots") == 0);
	assert(!ctx.entries[2].is_dir && strcmp(ctx.entries[2].name, "art.png") == 0);
	assert(!ctx.entries[3].is_dir && strcmp(ctx.entries[3].name, "photo.JPG") == 0);
	assert(ctx.entries[3].size_bytes == 1);

	// Subdir load: ".." first, then the file
	ImageBrowser_loadDirectory(&ctx, "/tmp/iv-test/Screenshots", "/tmp/iv-test");
	assert(ctx.entry_count == 2);
	assert(ctx.entries[0].is_dir && strcmp(ctx.entries[0].name, "..") == 0);
	assert(strcmp(ctx.entries[0].path, "/tmp/iv-test") == 0);
	assert(strcmp(ctx.entries[1].name, "a.jpeg") == 0);

	// Empty subdir: just ".."
	ImageBrowser_loadDirectory(&ctx, "/tmp/iv-test/Empty", "/tmp/iv-test");
	assert(ctx.entry_count == 1);

	// Display name strips extension
	char disp[64];
	ImageBrowser_getDisplayName("SCR_20260831_073912.jpg", disp, sizeof(disp));
	assert(strcmp(disp, "SCR_20260831_073912") == 0);

	ImageBrowser_freeEntries(&ctx);
	printf("test_iv_browser: all assertions passed\n");
	return 0;
}
