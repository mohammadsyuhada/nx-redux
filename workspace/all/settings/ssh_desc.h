#ifndef SSH_DESC_H
#define SSH_DESC_H

#include <stddef.h>

// Format the "Enable SSH" hint text into buf.
//
// running     - non-zero when sshd is up
// no_password - non-zero on platforms with no SSH password (tg5050)
// ip          - the device IP, or NULL/"" when not on a network
//
// Returns like snprintf: the number of characters that WOULD have been written
// (a value >= len means the text was truncated), or -1 on a NULL/zero buffer.
// Pure and host-compilable: no SDL, no api.h.
int dev_format_ssh_desc(char* buf, size_t len, int running, int no_password, const char* ip);

#endif // SSH_DESC_H
