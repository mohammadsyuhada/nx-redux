/*
 * USB Cable connection mode: one device's second (top) port to the other's
 * main (bottom) port. usblink.elf (workspace/all/usblink) does the link — this
 * file only starts it, waits for the handshake and stops it. The wizard never
 * learns which end is USB host: the daemon reports the peer's address, and
 * netplay host/client stays whatever the user picked on the role menu.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#include "api.h"
#include "defines.h"
#include "ui_buttonhintbar.h"
#include "ui_message.h"
#include "usblink_state.h"
#include "utils.h"
#include "wizard.h"

#define USBLINK_STATE_PATH "/tmp/usblink.state"
#define USBLINK_PID_PATH "/tmp/usblink.pid"

bool wiz_usb_link_running(void) {
	FILE* f = fopen(USBLINK_PID_PATH, "r");
	if (!f)
		return false;
	int pid = 0;
	bool alive = fscanf(f, "%d", &pid) == 1 && pid > 0 && kill(pid, 0) == 0;
	fclose(f);
	return alive;
}

void wiz_usb_link_stop(void) {
	// Always, not only when the pidfile looks alive: `stop` is also the repair
	// for a daemon that was SIGKILLed with the gadget function still linked.
	system("usblink.elf stop >/dev/null 2>&1");
}

static void wiz_usb_render(const char* line, bool cancelable) {
	char msg[256];
	snprintf(msg, sizeof(msg),
			 "Connect a USB-C cable from one device's\ntop port to the other device's\nbottom port.\n\n%s",
			 line);
	GFX_clear(wiz_screen);
	UI_renderCenteredMessage(wiz_screen, msg);
	if (cancelable)
		UI_renderButtonHintBar(wiz_screen, (char*[]){"B", "CANCEL", NULL});
	GFX_flip(wiz_screen);
}

// Terminal message, drawn the way wizard.c's show_message() draws its own.
static void wiz_usb_fail(const char* message, int hold_ms) {
	GFX_clear(wiz_screen);
	UI_renderCenteredMessage(wiz_screen, message);
	GFX_flip(wiz_screen);
	SDL_Delay(hold_ms);
	wiz_usb_link_stop();
}

int wiz_usb_link_up(WizSession* s) {
	// `start` blocks until the daemon publishes its first state (up to ~3 s);
	// put the cabling hint up first rather than leaving the mode menu on show
	// with B dead underneath it.
	wiz_usb_render("Starting USB link...", false);
	if (system("usblink.elf start >/dev/null 2>&1") != 0) {
		// A slow-starting daemon may still be alive after a failed start, so
		// the stop inside wiz_usb_fail() is not optional.
		wiz_usb_fail("USB link could not start.", 1500);
		return -1;
	}

	bool dirty = true;
	// No wall-clock ceiling: like the host's waiting screen, this waits for a
	// person to plug a cable in, and B is live every frame.
	while (1) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_B) || app_quit) {
			wiz_usb_link_stop();
			return -2;
		}

		UsbLinkState st;
		if (usblink_state_read(USBLINK_STATE_PATH, &st) == 0) {
			// The address ends up in a ping command line (wiz_client_known_host),
			// so anything that is not a dotted quad reads as not linked yet.
			struct in_addr addr;
			if (!strcmp(st.link, "up") && inet_pton(AF_INET, st.peer_ip, &addr) == 1) {
				snprintf(s->peer_ip, sizeof(s->peer_ip), "%s", st.peer_ip);
				return 0;
			}
			if (!strcmp(st.link, "error")) {
				const char* why = !strcmp(st.error, "version")
									  ? "Both devices need the same\nNXRedux version."
									  : "USB link unavailable\non this device.";
				wiz_usb_fail(why, 2000);
				return -1;
			}
		}

		PWR_update(&dirty, NULL, NULL, NULL);
		if (dirty) {
			wiz_usb_render("Waiting for the other device...", true);
			dirty = false;
		} else {
			GFX_sync();
		}
	}
}
