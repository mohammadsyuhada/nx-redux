// Host-compiled unit test for iv_fileops.c (no device toolchain needed).
// Build & run (from workspace/all/imageviewer):
//   cc -std=gnu99 iv_fileops.c tests/test_iv_fileops.c -o /tmp/test_iv_fileops && /tmp/test_iv_fileops
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../iv_fileops.h"

static bool exists(const char* path) {
	return access(path, F_OK) == 0;
}

int main(void) {
	system("rm -rf /tmp/iv-fops-test && mkdir -p /tmp/iv-fops-test/Sub");
	system("printf x > /tmp/iv-fops-test/a.jpg");
	system("printf x > /tmp/iv-fops-test/b.PNG");
	system("printf x > /tmp/iv-fops-test/c.jpg");
	system("printf x > /tmp/iv-fops-test/d.jpg");
	system("printf x > /tmp/iv-fops-test/e.jpg");
	system("printf x > /tmp/iv-fops-test/taken.jpg");
	system("printf x > /tmp/iv-fops-test/Sub/inner.txt");

	char out[512];

	// Plain rename, extension kept.
	memset(out, 0, sizeof(out));
	assert(IvFileops_renameFileKeepExt("/tmp/iv-fops-test/a.jpg", "newname", out, sizeof(out)));
	assert(strcmp(out, "/tmp/iv-fops-test/newname.jpg") == 0);
	assert(exists("/tmp/iv-fops-test/newname.jpg"));
	assert(!exists("/tmp/iv-fops-test/a.jpg"));

	// Case-preserving extension.
	memset(out, 0, sizeof(out));
	assert(IvFileops_renameFileKeepExt("/tmp/iv-fops-test/b.PNG", "x", out, sizeof(out)));
	assert(strcmp(out, "/tmp/iv-fops-test/x.PNG") == 0);
	assert(exists("/tmp/iv-fops-test/x.PNG"));
	assert(!exists("/tmp/iv-fops-test/b.PNG"));

	// Typed-extension strip: typing the same extension back is not a double-extension.
	memset(out, 0, sizeof(out));
	assert(IvFileops_renameFileKeepExt("/tmp/iv-fops-test/c.jpg", "photo.jpg", out, sizeof(out)));
	assert(strcmp(out, "/tmp/iv-fops-test/photo.jpg") == 0);
	assert(exists("/tmp/iv-fops-test/photo.jpg"));
	assert(!exists("/tmp/iv-fops-test/photo.jpg.jpg"));

	// Typed-extension strip is case-insensitive; original extension casing kept.
	memset(out, 0, sizeof(out));
	assert(IvFileops_renameFileKeepExt("/tmp/iv-fops-test/d.jpg", "shot.JPG", out, sizeof(out)));
	assert(strcmp(out, "/tmp/iv-fops-test/shot.jpg") == 0);
	assert(exists("/tmp/iv-fops-test/shot.jpg"));

	// Refusal: empty base.
	memset(out, 0, sizeof(out));
	assert(!IvFileops_renameFileKeepExt("/tmp/iv-fops-test/e.jpg", "", out, sizeof(out)));
	assert(exists("/tmp/iv-fops-test/e.jpg"));

	// Refusal: '/' in the typed name.
	memset(out, 0, sizeof(out));
	assert(!IvFileops_renameFileKeepExt("/tmp/iv-fops-test/e.jpg", "a/b", out, sizeof(out)));
	assert(exists("/tmp/iv-fops-test/e.jpg"));

	// Refusal: rename onto an existing target - both files untouched, and
	// out_path is filled with the colliding path so wiring can tell this
	// refusal apart from any other failure via access(out_path, F_OK).
	memset(out, 0, sizeof(out));
	assert(!IvFileops_renameFileKeepExt("/tmp/iv-fops-test/e.jpg", "taken", out, sizeof(out)));
	assert(exists("/tmp/iv-fops-test/e.jpg"));
	assert(exists("/tmp/iv-fops-test/taken.jpg"));
	assert(out[0] != '\0' && exists(out));

	// renameDir: moved with contents intact.
	memset(out, 0, sizeof(out));
	assert(IvFileops_renameDir("/tmp/iv-fops-test/Sub", "Renamed", out, sizeof(out)));
	assert(strcmp(out, "/tmp/iv-fops-test/Renamed") == 0);
	assert(exists("/tmp/iv-fops-test/Renamed/inner.txt"));
	assert(!exists("/tmp/iv-fops-test/Sub"));

	// renameDir refusal onto existing target, same out_path contract.
	system("mkdir -p /tmp/iv-fops-test/AlreadyThere");
	system("mkdir -p /tmp/iv-fops-test/MoveMe");
	memset(out, 0, sizeof(out));
	assert(!IvFileops_renameDir("/tmp/iv-fops-test/MoveMe", "AlreadyThere", out, sizeof(out)));
	assert(exists("/tmp/iv-fops-test/MoveMe"));
	assert(out[0] != '\0' && exists(out));

	// Guard: out_sz too small to hold the result path refuses instead of
	// silently truncating - and, critically, refuses BEFORE renaming
	// anything, so the source is left in place and no target is created
	// (a guard must never report failure for a rename that already
	// happened).
	system("printf x > /tmp/iv-fops-test/trunc.jpg");
	memset(out, 0, sizeof(out));
	assert(!IvFileops_renameFileKeepExt("/tmp/iv-fops-test/trunc.jpg", "trunclongname", out, 4));
	assert(exists("/tmp/iv-fops-test/trunc.jpg"));
	assert(!exists("/tmp/iv-fops-test/trunclongname.jpg"));

	// deleteFile: removes; missing file -> false.
	assert(IvFileops_deleteFile("/tmp/iv-fops-test/newname.jpg"));
	assert(!exists("/tmp/iv-fops-test/newname.jpg"));
	assert(!IvFileops_deleteFile("/tmp/iv-fops-test/newname.jpg"));

	// deleteTree: nested dirs with files at each level, fully removed.
	system("mkdir -p /tmp/iv-fops-test/t/x/y");
	system("printf x > /tmp/iv-fops-test/t/file0.txt");
	system("printf x > /tmp/iv-fops-test/t/x/file1.txt");
	system("printf x > /tmp/iv-fops-test/t/x/y/file2.txt");
	assert(IvFileops_deleteTree("/tmp/iv-fops-test/t"));
	assert(!exists("/tmp/iv-fops-test/t"));

	// deleteTree on a missing path -> false.
	assert(!IvFileops_deleteTree("/tmp/iv-fops-test/does-not-exist"));

	printf("test_iv_fileops: all assertions passed\n");
	return 0;
}
