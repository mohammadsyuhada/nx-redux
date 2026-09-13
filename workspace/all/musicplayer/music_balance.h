#ifndef MUSIC_BALANCE_H
#define MUSIC_BALANCE_H

int MusicBalance_getValue(void);
int MusicBalance_setValue(int value);
const char* MusicBalance_formatValue(int value);
const char* MusicBalance_getDisplayString(void);

#endif
