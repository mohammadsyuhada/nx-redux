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

#endif
