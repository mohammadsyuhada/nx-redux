// usblink_power.c - see usblink_power.h.
#include "usblink_power.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int usblink_power_parse(const char* s) {
	const char* eq = s ? strrchr(s, '=') : NULL;
	if (!eq)
		return -1;
	char* end;
	long v = strtol(eq + 1, &end, 0);
	if (end == eq + 1 || v < 0 || v > 0xff)
		return -1;
	return (int)v;
}

int usblink_power_limited_chg(int orig) {
	return orig & ~USBLINK_PMIC_CHG_EN;
}

int usblink_power_limited_ilim(int orig) {
	return orig & ~USBLINK_PMIC_ILIM_MASK;
}

int usblink_power_restored_chg(int orig) {
	return orig | USBLINK_PMIC_CHG_EN;
}

// tg5040 and tg5050 kernels name the node differently.
static const char* node(void) {
	static const char* const paths[] = {"/sys/class/axp/axp_reg", "/sys/class/axp/axp2202/axp_reg"};
	for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
		if (access(paths[i], W_OK) == 0)
			return paths[i];
	return NULL;
}

static bool limited;
static int orig_chg = -1, orig_ilim = -1;
static const char* reg_node;
// Prebuilt for usblink_power_restore_signal_safe (no formatting in a handler).
static char restore_chg[8], restore_ilim[8];

static bool write_str(const char* path, const char* s) {
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	bool ok = write(fd, s, strlen(s)) == (ssize_t)strlen(s);
	close(fd);
	return ok;
}

static int reg_read(int reg) {
	char sel[8], buf[64];
	snprintf(sel, sizeof(sel), "0x%02x", reg);
	if (!write_str(reg_node, sel))
		return -1;
	int fd = open(reg_node, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	return usblink_power_parse(buf);
}

static bool reg_write(int reg, int val) {
	char s[8];
	snprintf(s, sizeof(s), "0x%02x%02x", reg, val & 0xff);
	return write_str(reg_node, s);
}

static void save_file(void) {
	FILE* f = fopen(USBLINK_POWER_SAVE, "w");
	if (!f)
		return;
	fprintf(f, "%d %d\n", orig_chg, orig_ilim);
	fclose(f);
}

bool usblink_power_limit(void) {
	if (limited)
		return true;
	reg_node = node();
	if (!reg_node)
		return false;
	int chg = reg_read(USBLINK_PMIC_CHG_REG), ilim = reg_read(USBLINK_PMIC_ILIM_REG);
	if (chg < 0 || ilim < 0)
		return false;
	// A record left by a SIGKILLed run holds the real originals: ours would be
	// its limited values.
	FILE* f = fopen(USBLINK_POWER_SAVE, "r");
	int sc, si;
	if (f && fscanf(f, "%d %d", &sc, &si) == 2 && sc >= 0 && si >= 0) {
		chg = sc;
		ilim = si;
	}
	if (f)
		fclose(f);
	orig_chg = chg;
	orig_ilim = ilim;
	snprintf(restore_chg, sizeof(restore_chg), "0x%02x%02x", USBLINK_PMIC_CHG_REG, usblink_power_restored_chg(orig_chg));
	snprintf(restore_ilim, sizeof(restore_ilim), "0x%02x%02x", USBLINK_PMIC_ILIM_REG, orig_ilim);
	save_file();
	limited = true;
	usblink_power_check();
	fprintf(stderr, "usblink: charging from the peer off (0x19 0x%02x, 0x17 0x%02x)\n", orig_chg, orig_ilim);
	return true;
}

void usblink_power_check(void) {
	if (!limited)
		return;
	int chg = reg_read(USBLINK_PMIC_CHG_REG), ilim = reg_read(USBLINK_PMIC_ILIM_REG);
	if (chg >= 0 && chg != usblink_power_limited_chg(chg))
		reg_write(USBLINK_PMIC_CHG_REG, usblink_power_limited_chg(chg));
	if (ilim >= 0 && ilim != usblink_power_limited_ilim(ilim))
		reg_write(USBLINK_PMIC_ILIM_REG, usblink_power_limited_ilim(ilim));
}

void usblink_power_restore(void) {
	if (!limited)
		return;
	limited = false;
	reg_write(USBLINK_PMIC_CHG_REG, usblink_power_restored_chg(orig_chg));
	reg_write(USBLINK_PMIC_ILIM_REG, orig_ilim);
	unlink(USBLINK_POWER_SAVE);
	fprintf(stderr, "usblink: charging restored\n");
}

void usblink_power_restore_signal_safe(void) {
	if (!limited || !reg_node)
		return;
	write_str(reg_node, restore_chg);
	write_str(reg_node, restore_ilim);
	unlink(USBLINK_POWER_SAVE);
}

void usblink_power_restore_saved(void) {
	FILE* f = fopen(USBLINK_POWER_SAVE, "r");
	if (!f)
		return;
	int chg = -1, ilim = -1;
	int n = fscanf(f, "%d %d", &chg, &ilim);
	fclose(f);
	reg_node = node();
	if (reg_node && n == 2 && chg >= 0 && ilim >= 0) {
		reg_write(USBLINK_PMIC_CHG_REG, usblink_power_restored_chg(chg));
		reg_write(USBLINK_PMIC_ILIM_REG, ilim);
		fprintf(stderr, "usblink: charging restored after an unclean exit\n");
	}
	unlink(USBLINK_POWER_SAVE);
}

int usblink_cc_sink_value(int orig) {
	return (orig & ~(0x20 | USBLINK_PMIC_CC_MODE_MASK)) | 0x10 | USBLINK_PMIC_CC_SINK;
}

static bool cc_held;
static int orig_cc = -1, cc_state = -1;
// Prebuilt for usblink_cc_restore_signal_safe: the Disable step, then the original.
static char restore_cc_off[8], restore_cc[8];

// nanosleep: async-signal-safe, for the shutdown path too.
static void pause_ms(long ms) {
	struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
	nanosleep(&t, NULL);
}

// The vendor's "reset type-c": Disable for 50 ms, then [mode]; the port re-runs its attach.
static void cc_reset_to(int mode) {
	reg_write(USBLINK_PMIC_CC_MODE_REG, mode & ~USBLINK_PMIC_CC_MODE_MASK);
	pause_ms(50);
	reg_write(USBLINK_PMIC_CC_MODE_REG, mode);
}

// Sink only; already attached as the sink, the plain write keeps the live connection.
static void cc_apply_sink(void) {
	int st = reg_read(USBLINK_PMIC_CC_STATUS_REG);
	if (st >= 0 && (st & 0x0f) == USBLINK_PMIC_CC_ATTACHED_SINK)
		reg_write(USBLINK_PMIC_CC_MODE_REG, usblink_cc_sink_value(orig_cc));
	else
		cc_reset_to(usblink_cc_sink_value(orig_cc));
}

bool usblink_cc_hold_sink(void) {
	if (cc_held)
		return true;
	if (!reg_node)
		reg_node = node();
	if (!reg_node)
		return false;
	int cc = reg_read(USBLINK_PMIC_CC_MODE_REG);
	if (cc < 0)
		return false;
	// A record left by a SIGKILLed run holds the real original: ours would be
	// its held sink mode.
	FILE* f = fopen(USBLINK_CC_SAVE, "r");
	int saved;
	if (f && fscanf(f, "%d", &saved) == 1 && saved >= 0)
		cc = saved;
	if (f)
		fclose(f);
	if ((cc & USBLINK_PMIC_CC_MODE_MASK) != USBLINK_PMIC_CC_DRP) {
		fprintf(stderr, "usblink: main port USB-C mode 0x%02x is not dual-role; left alone\n", cc);
		return false;
	}
	orig_cc = cc;
	snprintf(restore_cc_off, sizeof(restore_cc_off), "0x%02x%02x", USBLINK_PMIC_CC_MODE_REG, orig_cc & ~USBLINK_PMIC_CC_MODE_MASK);
	snprintf(restore_cc, sizeof(restore_cc), "0x%02x%02x", USBLINK_PMIC_CC_MODE_REG, orig_cc);
	f = fopen(USBLINK_CC_SAVE, "w");
	if (f) {
		fprintf(f, "%d\n", orig_cc);
		fclose(f);
	}
	cc_held = true;
	cc_apply_sink();
	fprintf(stderr, "usblink: main port held as USB device (0xe3 0x%02x -> 0x%02x)\n", orig_cc,
			usblink_cc_sink_value(orig_cc));
	return true;
}

void usblink_cc_check(void) {
	if (!cc_held)
		return;
	int cc = reg_read(USBLINK_PMIC_CC_MODE_REG);
	if (cc >= 0 && (cc & USBLINK_PMIC_CC_MODE_MASK) != USBLINK_PMIC_CC_SINK) {
		fprintf(stderr, "usblink: main port USB-C mode reset to 0x%02x; held as device again\n", cc);
		cc_apply_sink();
	}
	int st = reg_read(USBLINK_PMIC_CC_STATUS_REG);
	if (st >= 0 && (st & 0x0f) != cc_state) {
		cc_state = st & 0x0f;
		fprintf(stderr, "usblink: main port USB-C state %d (3 attached as device, 6 as host)\n", cc_state);
	}
}

void usblink_cc_restore(void) {
	if (!cc_held)
		return;
	cc_held = false;
	cc_reset_to(orig_cc);
	unlink(USBLINK_CC_SAVE);
	fprintf(stderr, "usblink: main port USB-C mode back to 0x%02x\n", orig_cc);
}

void usblink_cc_restore_signal_safe(void) {
	if (!cc_held || !reg_node)
		return;
	write_str(reg_node, restore_cc_off);
	pause_ms(50);
	write_str(reg_node, restore_cc);
	unlink(USBLINK_CC_SAVE);
}

void usblink_cc_restore_saved(void) {
	FILE* f = fopen(USBLINK_CC_SAVE, "r");
	if (!f)
		return;
	int cc = -1;
	int n = fscanf(f, "%d", &cc);
	fclose(f);
	if (!reg_node)
		reg_node = node();
	if (reg_node && n == 1 && cc >= 0 && cc <= 0xff) {
		cc_reset_to(cc);
		fprintf(stderr, "usblink: main port USB-C mode back to 0x%02x after an unclean exit\n", cc);
	}
	unlink(USBLINK_CC_SAVE);
}
