#ifndef USBLINK_TUN_H
#define USBLINK_TUN_H

// Point-to-point TUN interface carrying the IP packets that cross the cable.
// Addresses are set with ioctls (no ifconfig/ip dependency on the device).
int usblink_tun_ensure_module(const char* ko_path);									 // 0 if /dev/net/tun exists or the module loaded; -1 otherwise
int usblink_tun_open(const char* name);												 // fd (IFF_TUN|IFF_NO_PI), -1 on error
int usblink_tun_set_up(const char* name, const char* local_ip, const char* peer_ip); // addr, dstaddr, /30, mtu 1500, IFF_UP; 0/-1
int usblink_tun_set_down(const char* name);											 // clear IFF_UP and the address; 0/-1

#endif
