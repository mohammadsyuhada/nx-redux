// usblink_power.c - see usblink_power.h.
#include "usblink_power.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
