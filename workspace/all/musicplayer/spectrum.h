#ifndef __SPECTRUM_H__
#define __SPECTRUM_H__

#include <stdbool.h>

#define SPECTRUM_FFT_SIZE 512
#define SPECTRUM_BARS 64
#define LAYER_SPECTRUM 5

typedef enum {
	SPECTRUM_STYLE_VERTICAL = 0, // Vertical gradient within each bar (default)
	SPECTRUM_STYLE_WHITE,		 // White bars
	SPECTRUM_STYLE_RAINBOW,		 // Rainbow gradient across bars
	SPECTRUM_STYLE_MAGNITUDE,	 // Green (low) to red (high) like VU meter
	SPECTRUM_STYLE_COUNT
} SpectrumStyle;

typedef struct {
	float bars[SPECTRUM_BARS];
	float peaks[SPECTRUM_BARS];
	bool valid;
} SpectrumData;

void Spectrum_init(void);
void Spectrum_quit(void);
void Spectrum_update(void);

void Spectrum_setPosition(int x, int y, int w, int h);
void Spectrum_renderGPU(void);
bool Spectrum_needsRefresh(void);

// Combined cycle: rotates through all 4 styles, then off
void Spectrum_cycleNext(void);

#endif
