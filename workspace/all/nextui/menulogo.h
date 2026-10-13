#ifndef MENULOGO_H
#define MENULOGO_H
#include <stddef.h>
// Logo id (the <id> in res/menu/menu_logo_<id>.png) for a console folder name such as
// "Game Boy Advance (MGBA)" or "Sega Genesis (GPGX)"; NULL when there is no bundled logo.
const char* MenuLogo_idForFolder(const char* folder_name);
// The logo file for a console folder (its full path), written to out: the user's own <folder>/.media/logo.png (an
// absolute path) when there is one, so a custom system gets a logo and a stock one can be replaced, else the bundled
// "menu_logo_<id>.png" (a res/menu name), else NULL. Whether the user's file is there is looked up once per folder,
// until MenuLogo_forget (a new menu generation: a rescan or a tab switch).
const char* MenuLogo_fileFor(const char* folder_path, char* out, size_t size);
void MenuLogo_forget(void);
#endif
