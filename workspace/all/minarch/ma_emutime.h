#ifndef MA_EMUTIME_H
#define MA_EMUTIME_H
// Emulated time for the "Emulated" Core Sync mode: how much game time the
// core covered since the last present, measured from the audio it produced.
// A GPU core like flycast can cover several vblanks in one retro_run (a
// 30 fps game covers two), which vsync or fixed 1/fps pacing cannot see.
#include <stddef.h>

void EmuTime_countAudio(size_t frames);
double EmuTime_takeSlot(double sample_rate, double fps);
void EmuTime_reset(void);
unsigned long long EmuTime_totalFrames(void);
#endif
