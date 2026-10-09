// wiz_modes.h - which connection modes the wizard offers.
//
// A pak's launcher can pass --modes <csv> to limit the Connection menu, e.g.
// mGBA's link cable passes "usb" because it only runs over the USB Cable
// transport. Without --modes the wizard offers all three.
//
// Libc only, so the host unit test links it directly.

#ifndef WIZ_MODES_H
#define WIZ_MODES_H

#define WIZ_MODES_ALL 0x7u // bit i = mode_keys[i]: 0 usb, 1 hotspot, 2 wifi

// Parses a csv of "usb","hotspot","wifi" into a mask. Returns the mask, or 0
// on an empty string, an unknown word, or a duplicate.
unsigned WizModes_parse(const char* csv);
// Fills rows[] with the indices (0..2) of the set bits, in key order; returns the count.
int WizModes_rows(unsigned mask, int rows[3]);

#endif
