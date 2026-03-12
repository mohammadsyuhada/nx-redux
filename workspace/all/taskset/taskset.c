/*
 * Minimal taskset implementation for embedded Linux (aarch64).
 *
 * Supports:
 *   taskset MASK command [args...]        — launch with hex mask
 *   taskset -c LIST command [args...]     — launch with CPU list
 *   taskset -p MASK PID                   — set PID affinity (hex mask)
 *   taskset -p -c LIST PID               — set PID affinity (CPU list)
 */

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static int parse_cpu_list(const char* list, cpu_set_t* set) {
	CPU_ZERO(set);
	const char* p = list;
	while (*p) {
		char* end;
		unsigned long start = strtoul(p, &end, 10);
		if (end == p)
			return -1;
		if (*end == '-') {
			p = end + 1;
			unsigned long stop = strtoul(p, &end, 10);
			if (end == p)
				return -1;
			for (unsigned long i = start; i <= stop; i++)
				CPU_SET(i, set);
		} else {
			CPU_SET(start, set);
		}
		if (*end == ',')
			end++;
		p = end;
	}
	return 0;
}

static int parse_hex_mask(const char* mask, cpu_set_t* set) {
	CPU_ZERO(set);
	/* skip optional 0x prefix */
	if (mask[0] == '0' && (mask[1] == 'x' || mask[1] == 'X'))
		mask += 2;
	char* end;
	unsigned long val = strtoul(mask, &end, 16);
	if (*end != '\0')
		return -1;
	for (int i = 0; i < (int)(sizeof(unsigned long) * 8); i++) {
		if (val & (1UL << i))
			CPU_SET(i, set);
	}
	return 0;
}

int main(int argc, char* argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: taskset [-p] [-c] MASK|LIST [PID|cmd [args...]]\n");
		return 1;
	}

	int opt_p = 0, opt_c = 0;
	int i = 1;

	while (i < argc && argv[i][0] == '-') {
		for (const char* f = argv[i] + 1; *f; f++) {
			if (*f == 'p')
				opt_p = 1;
			else if (*f == 'c')
				opt_c = 1;
			else {
				fprintf(stderr, "taskset: unknown option -%c\n", *f);
				return 1;
			}
		}
		i++;
	}

	if (i >= argc) {
		fprintf(stderr, "taskset: missing mask/list\n");
		return 1;
	}

	const char* maskstr = argv[i++];
	cpu_set_t set;

	if (opt_c) {
		if (parse_cpu_list(maskstr, &set) < 0) {
			fprintf(stderr, "taskset: bad CPU list '%s'\n", maskstr);
			return 1;
		}
	} else {
		if (parse_hex_mask(maskstr, &set) < 0) {
			fprintf(stderr, "taskset: bad mask '%s'\n", maskstr);
			return 1;
		}
	}

	if (opt_p) {
		/* set affinity of existing PID */
		if (i >= argc) {
			fprintf(stderr, "taskset: missing PID\n");
			return 1;
		}
		pid_t pid = atoi(argv[i]);
		if (sched_setaffinity(pid, sizeof(set), &set) < 0) {
			fprintf(stderr, "taskset: sched_setaffinity(%d): %s\n", pid, strerror(errno));
			return 1;
		}
		return 0;
	}

	/* launch command with affinity */
	if (i >= argc) {
		fprintf(stderr, "taskset: missing command\n");
		return 1;
	}

	if (sched_setaffinity(0, sizeof(set), &set) < 0) {
		fprintf(stderr, "taskset: sched_setaffinity: %s\n", strerror(errno));
		return 1;
	}
	execvp(argv[i], &argv[i]);
	fprintf(stderr, "taskset: exec %s: %s\n", argv[i], strerror(errno));
	return 127;
}
