#pragma once

void Game_open(char* path);
void Game_close(void);
int Game_changeDisc(char* path); // returns 1 if the requested disc is loaded afterward
int extract_zip(char** extensions);
int extract_7z(char** extensions);
