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

// --- the main port's USB-C role (AXP717 datasheet §6.15.2.103-104) ---
//
// The link needs the main port to be the USB device. On tg5050 that port is a
// dual-role Type-C port run by the PMIC's own CC logic (the board's TCPC does
// not probe, so the typec sysfs knobs do nothing), and against a phone, also
// dual-role and preferring sink, it often attaches as the source / host, where
// nothing links. While the daemon runs the port is held sink only: reg 0xE3
// bits 1:0 = 01 (00 disabled, 11 DRP), bit 4 Try.SNK, bit 5 Try.SRC, bits 3:2
// the Rp current (kept). A mode change takes effect through a short Disable
// (the vendor driver's 50 ms "reset type-c"), which re-runs the attach. The
// vendor driver puts DRP back when VBUS goes away, so the hold is re-checked
// with usblink_power_check's cadence. reg 0xE7 bits 3:0: 3 attached as sink,
// 6 attached as source.
#define USBLINK_PMIC_CC_MODE_REG 0xe3
#define USBLINK_PMIC_CC_STATUS_REG 0xe7
#define USBLINK_PMIC_CC_MODE_MASK 0x03
#define USBLINK_PMIC_CC_DRP 0x03
#define USBLINK_PMIC_CC_SINK 0x01
#define USBLINK_PMIC_CC_ATTACHED_SINK 3
#define USBLINK_CC_SAVE "/tmp/usblink.cc"

// Sink only with Try.SNK, Try.SRC off and the Rp bits kept (pure, host-tested).
int usblink_cc_sink_value(int orig);
// Hold the main port sink only; remembers the original 0xE3 once. False: no
// PMIC node, or the port is not dual-role (left alone).
bool usblink_cc_hold_sink(void);
// While held: re-apply after the driver reset it, and log the attach state.
void usblink_cc_check(void);
// Put the original mode back. No-op when not held.
void usblink_cc_restore(void);
// Async-signal-safe restore for the shutdown path.
void usblink_cc_restore_signal_safe(void);
// `usblink stop` after a SIGKILLed daemon: restore from USBLINK_CC_SAVE.
void usblink_cc_restore_saved(void);

#endif
