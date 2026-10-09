// netplay_ports.h - which player's input a libretro port reads in a lockstep
// netplay session (netplay.c Netplay_getInputState).
#ifndef NETPLAY_PORTS_H
#define NETPLAY_PORTS_H

#include <stdint.h>

// The engine carries two players: host = port 0, client = port 1. Further
// ports (multitap, 4-player games) have nobody on them and read 0, as in
// local play; returning player 2 there made players 3+ mirror player 2.
static inline uint16_t netplay_port_input(unsigned port, uint16_t p1, uint16_t p2) {
	return port == 0 ? p1 : port == 1 ? p2
									  : 0;
}

// Stick axes each player carries: left X/Y, right X/Y. Slot = index * 2 + id
// (RETRO_DEVICE_INDEX_ANALOG_LEFT/RIGHT, RETRO_DEVICE_ID_ANALOG_X/Y), -1 for
// anything else (the analog-button index reads 0).
#define NETPLAY_ANALOG_AXES 4
static inline int netplay_analog_slot(unsigned index, unsigned id) {
	return (index <= 1 && id <= 1) ? (int)(index * 2 + id) : -1;
}

// Same ports as the buttons: host sticks on port 0, client sticks on port 1.
static inline int16_t netplay_port_analog(unsigned port, unsigned index, unsigned id,
										  const int16_t* p1, const int16_t* p2) {
	int slot = netplay_analog_slot(index, id);
	if (slot < 0)
		return 0;
	return port == 0 ? p1[slot] : port == 1 ? p2[slot]
											: 0;
}

#endif
