#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "config.h"

void printUsage() {
	printf("usage: nextval <key>\n");
}

int main(int argc, char* argv[]) {
	CFG_init(NULL, NULL);

	if (argc <= 1) {
		CFG_print();
		return EXIT_SUCCESS;
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		printUsage();
	}
	// get
	else if (argc == 2) {
		char setting_value[MAX_PATH];
		CFG_get(argv[1], setting_value);
		if (strcmp(setting_value, "") != 0)
			printf("{\"%s\": %s}\n", argv[1], setting_value);
		else
			printf("{}\n");
	} else {
		printf("Error: Invalid argument '%s'\n", argv[1]);
		printUsage();
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}