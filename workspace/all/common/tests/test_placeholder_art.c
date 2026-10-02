// Host test for nextui/placeholder_art.c: opaque, deterministic per seed, different seeds differ, pitch respected.
#include "../../nextui/placeholder_art.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W 64
#define H 40

int main(void) {
	static uint32_t a[W * H], b[W * H], c[W * H];
	PlaceholderArt_render(a, W, H, W, "Mario Kart - Super Circuit");
	PlaceholderArt_render(b, W, H, W, "Mario Kart - Super Circuit");
	PlaceholderArt_render(c, W, H, W, "Golden Sun");
	assert(memcmp(a, b, sizeof(a)) == 0); // the same game keeps its picture
	assert(memcmp(a, c, sizeof(a)) != 0); // another game gets another
	for (int i = 0; i < W * H; i++)
		assert((a[i] >> 24) == 0xFF); // opaque
	// dark enough for white captions: the mean channel stays under half
	unsigned long sum = 0;
	for (int i = 0; i < W * H; i++)
		sum += ((a[i] >> 16) & 0xFF) + ((a[i] >> 8) & 0xFF) + (a[i] & 0xFF);
	assert(sum / (3UL * W * H) < 128);
	// a padded pitch: the pixels past w stay untouched
	static uint32_t p[(W + 8) * H];
	memset(p, 0xAB, sizeof(p));
	PlaceholderArt_render(p, W, H, W + 8, "x");
	for (int y = 0; y < H; y++)
		for (int x = W; x < W + 8; x++)
			assert(p[y * (W + 8) + x] == 0xABABABABu);
	PlaceholderArt_render(NULL, W, H, W, "x"); // refused, no crash
	PlaceholderArt_render(a, 0, H, W, NULL);
	printf("test_placeholder_art: all passed\n");
	return 0;
}
