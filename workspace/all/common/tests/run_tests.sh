#!/bin/sh
# Host-side unit tests for common/. No SDL, no cross toolchain.
set -eu
cd "$(dirname "$0")"
cc -std=gnu99 -Wall -Werror -o /tmp/nx_test_paths ../paths.c test_paths.c
/tmp/nx_test_paths
cc -std=gnu99 -Wall -Werror -o /tmp/nx_test_probe ../desktop_probe.c test_desktop_probe.c && /tmp/nx_test_probe
cc -std=gnu99 -Wall -Werror -o /tmp/nx_test_xtras_compat ../../extras/xtras_compat.c test_xtras_compat.c && /tmp/nx_test_xtras_compat
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_text_wrap test_text_wrap.c && /tmp/nx_test_text_wrap
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_initial_jump test_initial_jump.c && /tmp/nx_test_initial_jump
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_scraper_scan ../../scraper/scraper_scan.c test_scraper_scan.c && /tmp/nx_test_scraper_scan
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_scraper_systems ../../scraper/scraper_systems.c test_scraper_systems.c && /tmp/nx_test_scraper_systems
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_arcade_names ../arcade_names.c test_arcade_names.c && /tmp/nx_test_arcade_names
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_button_layout test_button_layout.c && /tmp/nx_test_button_layout
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_next_cmd test_next_cmd.c && /tmp/nx_test_next_cmd
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_gpu_governor_hold test_gpu_governor_hold.c && /tmp/nx_test_gpu_governor_hold
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_ui_scale test_ui_scale.c && /tmp/nx_test_ui_scale
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_wiz_caps ../../netplay-wizard/wiz_caps.c test_wiz_caps.c && /tmp/nx_test_wiz_caps
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_core_netplay ../../netplay/core_netplay.c test_core_netplay.c && /tmp/nx_test_core_netplay
sh test_launcher_logs_path.sh
