// placeholder_art.c — an abstract picture for a game without a screenshot. SDL-free.
#include "placeholder_art.h"
#include <math.h>
#include <stddef.h>

static uint32_t fnv(const char* s) {
	uint32_t h = 2166136261u;
	for (; s && *s; s++)
		h = (h ^ (uint8_t)*s) * 16777619u;
	return h;
}

// hue in [0, 1), s and v in [0, 1] → rgb in [0, 1]
static void hsv(float hue, float s, float v, float out[3]) {
	float h6 = hue * 6.0f;
	int i = (int)h6 % 6;
	float f = h6 - floorf(h6), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
	const float tab[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
	out[0] = tab[i][0], out[1] = tab[i][1], out[2] = tab[i][2];
}

static float smooth(float x) {
	x = x < 0 ? 0 : (x > 1 ? 1 : x);
	return x * x * (3 - 2 * x);
}

// The picture for the shapes' seed hs in hue (and hue2, the second; equal for one hue), saturation × sat.
static void render(uint32_t* px, int w, int h, int pitch, uint32_t hs, float hue, float hue2, float sat) {
	float c1[3], c2[3], g1[3], g2[3];
	hsv(hue, 0.62f * sat, 0.40f, c1);  // the gradient's start
	hsv(hue2, 0.58f * sat, 0.24f, c2); // ...and its darker end
	hsv(hue2, 0.50f * sat, 0.66f, g1); // a glow in the second hue
	hsv(hue, 0.42f * sat, 0.72f, g2);  // a lighter glow in the main hue
	float gx[2], gy[2], gr[2];
	for (int k = 0; k < 2; k++) {
		gx[k] = ((hs >> (3 + k * 7)) % 100) / 100.0f;
		gy[k] = ((hs >> (11 + k * 5)) % 100) / 100.0f;
		gr[k] = (0.55f + ((hs >> (17 + k * 3)) % 30) / 100.0f) * h; // px
	}
	const float ga[2] = {0.45f, 0.35f};
	float* g[2] = {g1, g2};
	// three large translucent discs (white at 5-7%), placed by the seed: the abstract shapes
	float dx_[3], dy_[3], dr[3];
	for (int k = 0; k < 3; k++) {
		uint32_t q = hs * (2654435761u + 40503u * (uint32_t)k);
		dx_[k] = (q % 1000) / 1000.0f * w;
		dy_[k] = ((q >> 10) % 1000) / 1000.0f * h;
		dr[k] = (0.25f + ((q >> 20) % 40) / 100.0f) * h;
	}
	int line_step = h / 15 > 6 ? h / 15 : 6; // the faint diagonal lines' period
	float line_w = line_step * 0.07f + 0.6f;
	for (int y = 0; y < h; y++) {
		uint32_t* row = px + (size_t)y * pitch;
		float ny = (float)y / h;
		for (int x = 0; x < w; x++) {
			float nx = (float)x / w;
			float t = nx * 0.6f + ny * 0.4f;
			float c[3];
			for (int i = 0; i < 3; i++)
				c[i] = c1[i] + (c2[i] - c1[i]) * t;
			for (int k = 0; k < 2; k++) {
				float dx = (nx - gx[k]) * w, dy = (ny - gy[k]) * h;
				float f = smooth(1 - sqrtf(dx * dx + dy * dy) / gr[k]) * ga[k];
				for (int i = 0; i < 3; i++)
					c[i] += (g[k][i] - c[i]) * f;
			}
			float on = fmodf((float)(x + y), (float)line_step) < line_w ? 0.05f : 0.0f;
			for (int k = 0; k < 3; k++) {
				float ex = x - dx_[k], ey = y - dy_[k];
				float d = sqrtf(ex * ex + ey * ey) - dr[k]; // px from the disc's edge (< 0 inside)
				float cov = 0.5f - d;						// its edge anti-aliased over a px
				cov = cov < 0 ? 0 : (cov > 1 ? 1 : cov);
				float rim = 1.0f - fabsf(d) / 1.5f; // the rim a touch brighter, also soft
				on += cov * (0.05f + 0.02f * (k == 0)) + (rim > 0 ? rim * 0.06f : 0.0f);
			}
			float vx = nx - 0.5f, vy = ny - 0.5f;
			float v = 0.75f + 0.25f * (1 - sqrtf(vx * vx + vy * vy) / 0.7f); // vignette
			uint32_t o = 0xFF000000u;
			for (int i = 0; i < 3; i++) {
				float ch = (c[i] + (1 - c[i]) * on) * v;
				int b = (int)(ch * 255 + 0.5f);
				o |= (uint32_t)(b < 0 ? 0 : (b > 255 ? 255 : b)) << (16 - 8 * i);
			}
			row[x] = o;
		}
	}
}

void PlaceholderArt_render(uint32_t* px, int w, int h, int pitch, const char* seed) {
	if (!px || w <= 0 || h <= 0)
		return;
	uint32_t hs = fnv(seed);
	float hue = (hs % 360) / 360.0f;
	render(px, w, h, pitch, hs, hue, fmodf(hue + (40 + (hs >> 9) % 60) / 360.0f, 1.0f), 1.0f);
}

void PlaceholderArt_renderRgb(uint32_t* px, int w, int h, int pitch, const char* seed, uint32_t rgb) {
	if (!px || w <= 0 || h <= 0)
		return;
	float r = ((rgb >> 16) & 0xFF) / 255.0f, g = ((rgb >> 8) & 0xFF) / 255.0f, b = (rgb & 0xFF) / 255.0f;
	float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
	float hue = 0;
	if (d > 0) {
		if (mx == r)
			hue = fmodf((g - b) / d, 6.0f);
		else if (mx == g)
			hue = (b - r) / d + 2.0f;
		else
			hue = (r - g) / d + 4.0f;
		hue /= 6.0f;
		if (hue < 0)
			hue += 1.0f;
	}
	float sat = mx > 0 ? d / mx : 0; // a grey or white accent: a neutral picture
	render(px, w, h, pitch, fnv(seed), hue, hue, sat);
}
