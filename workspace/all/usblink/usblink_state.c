#include "usblink_state.h"

#include <stdio.h>
#include <string.h>

#define HOST_IP "10.99.0.1"
#define DEVICE_IP "10.99.0.2"

void usblink_state_from_link(const UsbLink* l, const char* error, UsbLinkState* out) {
	memset(out, 0, sizeof(*out));
	snprintf(out->error, sizeof(out->error), "%s", error ? error : "");
	if (l->phase == USBLINK_ERROR) {
		strcpy(out->link, "error");
		return;
	}
	if (l->phase != USBLINK_UP) {
		strcpy(out->link, "down");
		return;
	}
	strcpy(out->link, "up");
	bool host = (l->side == USBLINK_SIDE_HOST);
	strcpy(out->side, host ? "host" : "device");
	strcpy(out->local_ip, host ? HOST_IP : DEVICE_IP);
	strcpy(out->peer_ip, host ? DEVICE_IP : HOST_IP);
}

// Written to a temp file and renamed, so a reader (the wizard polls this every
// frame) never sees half a file.
int usblink_state_write(const char* path, const UsbLinkState* s) {
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* f = fopen(tmp, "w");
	if (!f)
		return -1;
	fprintf(f, "USBLINK_LINK=%s\nUSBLINK_SIDE=%s\nUSBLINK_LOCAL_IP=%s\nUSBLINK_PEER_IP=%s\nUSBLINK_ERROR=%s\n",
			s->link, s->side, s->local_ip, s->peer_ip, s->error);
	if (fclose(f) != 0)
		return -1;
	return rename(tmp, path) == 0 ? 0 : -1;
}

static void take(const char* line, const char* key, char* dst, size_t size) {
	size_t k = strlen(key);
	if (strncmp(line, key, k) || line[k] != '=')
		return;
	snprintf(dst, size, "%s", line + k + 1);
	dst[strcspn(dst, "\r\n")] = '\0';
}

int usblink_state_read(const char* path, UsbLinkState* s) {
	memset(s, 0, sizeof(*s));
	FILE* f = fopen(path, "r");
	if (!f)
		return -1;
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		take(line, "USBLINK_LINK", s->link, sizeof(s->link));
		take(line, "USBLINK_SIDE", s->side, sizeof(s->side));
		take(line, "USBLINK_LOCAL_IP", s->local_ip, sizeof(s->local_ip));
		take(line, "USBLINK_PEER_IP", s->peer_ip, sizeof(s->peer_ip));
		take(line, "USBLINK_ERROR", s->error, sizeof(s->error));
	}
	fclose(f);
	return s->link[0] ? 0 : -1;
}
