#ifndef USBLINK_GADGET_H
#define USBLINK_GADGET_H

// FunctionFS "nxlink" interface added next to adb in the stock configfs gadget
// (g1/configs/c.1), so the device side of the cable shows up to the peer's
// USB host as a vendor bulk interface while adb keeps working.
int usblink_gadget_attach(int* out_fd, int* in_fd); // ep1 (host->us), ep2 (us->host); spawns nothing; 0/-1
int usblink_gadget_ep0_fd(void);					// for the event-drain thread; -1 when not attached
void usblink_gadget_detach(void);					// async-signal-safe: unbind, unlink, rebind, close, umount; idempotent
void usblink_gadget_repair(void);					// for `stop`: works without the daemon's in-memory state

#endif
