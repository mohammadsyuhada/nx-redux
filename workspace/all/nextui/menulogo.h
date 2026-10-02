#ifndef MENULOGO_H
#define MENULOGO_H
// Logo id (the <id> in res/menu/menu_logo_<id>.png) for a console folder name such as
// "Game Boy Advance (MGBA)" or "Sega Genesis (GPGX)"; NULL when there is no bundled logo.
const char* MenuLogo_idForFolder(const char* folder_name);
#endif
