#!/bin/sh
# Host-side unit tests for common/. No SDL, no cross toolchain.
set -eu
cd "$(dirname "$0")"
cc -std=gnu99 -Wall -Werror -o /tmp/nx_test_paths ../paths.c test_paths.c
/tmp/nx_test_paths
cc -std=gnu99 -Wall -Werror -o /tmp/nx_test_probe ../desktop_probe.c test_desktop_probe.c && /tmp/nx_test_probe
cc -std=gnu99 -Wall -Werror -o /tmp/nx_test_xtras_compat ../../extras/xtras_compat.c test_xtras_compat.c && /tmp/nx_test_xtras_compat
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_text_wrap test_text_wrap.c && /tmp/nx_test_text_wrap
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_scraper_scan ../../scraper/scraper_scan.c test_scraper_scan.c && /tmp/nx_test_scraper_scan
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_scraper_systems ../../scraper/scraper_systems.c test_scraper_systems.c && /tmp/nx_test_scraper_systems
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_arcade_names ../../nextui/arcade_names.c test_arcade_names.c && /tmp/nx_test_arcade_names
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_button_layout test_button_layout.c && /tmp/nx_test_button_layout
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_next_cmd test_next_cmd.c && /tmp/nx_test_next_cmd
sh test_launcher_logs_path.sh
