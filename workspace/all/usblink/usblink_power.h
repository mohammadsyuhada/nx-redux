// usblink_power.h - keep the peer from charging us over the link cable.
//
// The device whose main (bottom) port carries the link is powered by the
// other device's host port. Charging its battery from there (up to the
// "usb pc" 500 mA limit, plus the game) sags the peer's VBUS and drops the
// link mid-session. While the link is up through our gadget port we turn the
// charger off and cap the input current at the minimum, then put both back.
//
// Both platforms use an X-Powers AXP2202 (= mainline AXP717): reg 0x19 bit 1
// enables the charger, reg 0x17 bits 0-5 set the input current limit
// (100 mA + 50 mA * n). They are reached through the vendor debug node, which
// takes "0xRR" (select) or "0xRRVV" (write) and reads back "...REG[0xRR]=0xVV".
#ifndef USBLINK_POWER_H
#define USBLINK_POWER_H

#include <stdbool.h>

#define USBLINK_PMIC_CHG_REG 0x19
#define USBLINK_PMIC_CHG_EN 0x02
#define USBLINK_PMIC_ILIM_REG 0x17
#define USBLINK_PMIC_ILIM_MASK 0x3f // 0 = 100 mA

// Original registers kept for the restore (also on disk for `usblink stop`).
#define USBLINK_POWER_SAVE "/tmp/usblink.power"

// --- pure helpers (host-tested) ---
// Value from a debug-node read ("axp2202-REG[0x19]=0x6"), or -1.
int usblink_power_parse(const char* s);
int usblink_power_limited_chg(int orig);  // charger off
int usblink_power_limited_ilim(int orig); // 100 mA, other bits kept
int usblink_power_restored_chg(int orig); // charger always back on

// --- device I/O ---
// Turn charging off; remembers the originals once. False: no PMIC node.
bool usblink_power_limit(void);
// While limited: re-apply what the kernel driver rewrote (it resets the input
// limit on every replug). Cheap; call about once a second.
void usblink_power_check(void);
// Put the originals back (charger on). No-op when not limited.
void usblink_power_restore(void);
// Async-signal-safe restore for the shutdown path.
void usblink_power_restore_signal_safe(void);
// `usblink stop` after a SIGKILLed daemon: restore from USBLINK_POWER_SAVE.
void usblink_power_restore_saved(void);

#endif
