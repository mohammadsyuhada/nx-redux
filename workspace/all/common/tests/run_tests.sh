#!/bin/sh
# Host-side unit tests for common/. No SDL, no cross toolchain.
set -eu
cd "$(dirname "$0")"
# Compile and run on separate lines: set -e ignores a failure on the left of &&, so a test that
# fails to compile would otherwise be skipped silently.
cc -std=gnu99 -Wall -Werror -o /tmp/nx_test_xtras_compat ../../extras/xtras_compat.c test_xtras_compat.c
/tmp/nx_test_xtras_compat
cc -std=gnu99 -Wall -Werror -Wno-deprecated-declarations -fsanitize=address -g -I.. -I../../../tg5040/platform -DPLATFORM=\"tg5040\" -o /tmp/nx_test_art_path ../utils.c test_art_path.c
/tmp/nx_test_art_path
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_text_wrap test_text_wrap.c
/tmp/nx_test_text_wrap
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_title_fit test_title_fit.c
/tmp/nx_test_title_fit
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_list_layout test_list_layout.c
/tmp/nx_test_list_layout
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_initial_jump test_initial_jump.c
/tmp/nx_test_initial_jump
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_scraper_scan ../../scraper/scraper_scan.c test_scraper_scan.c
/tmp/nx_test_scraper_scan
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_scraper_systems ../../scraper/scraper_systems.c test_scraper_systems.c
/tmp/nx_test_scraper_systems
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_arcade_names ../arcade_names.c test_arcade_names.c
/tmp/nx_test_arcade_names
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_button_layout test_button_layout.c
/tmp/nx_test_button_layout
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_next_cmd test_next_cmd.c
/tmp/nx_test_next_cmd
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_gpu_governor_hold test_gpu_governor_hold.c
/tmp/nx_test_gpu_governor_hold
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_ui_scale test_ui_scale.c
/tmp/nx_test_ui_scale
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_wiz_caps ../../netplay-wizard/wiz_caps.c test_wiz_caps.c
/tmp/nx_test_wiz_caps
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_core_netplay ../../netplay/core_netplay.c test_core_netplay.c
/tmp/nx_test_core_netplay
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_menutabs_model ../../nextui/menutabs_model.c test_menutabs_model.c
/tmp/nx_test_menutabs_model
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_list_window ../../nextui/list_window.c test_list_window.c
/tmp/nx_test_list_window
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_emulist_model ../../nextui/emulist_model.c test_emulist_model.c
/tmp/nx_test_emulist_model
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_gameinfo_text ../../nextui/gameinfo_text.c test_gameinfo_text.c
/tmp/nx_test_gameinfo_text
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_menulogo ../../nextui/menulogo.c test_menulogo.c
/tmp/nx_test_menulogo
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_ui_ease ../ui/ui_ease.c test_ui_ease.c -lm
/tmp/nx_test_ui_ease
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -DUI_ACCENT_NO_SDL -o /tmp/nx_test_accent ../ui/ui_accent.c test_accent.c -lm
/tmp/nx_test_accent
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I../ui -o /tmp/nx_test_infoband_layout ../../nextui/infoband_layout.c test_infoband_layout.c
/tmp/nx_test_infoband_layout
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_collcount_model ../../nextui/collcount_model.c test_collcount_model.c
/tmp/nx_test_collcount_model
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_collname ../../nextui/collname.c test_collname.c
/tmp/nx_test_collname
sh test_launcher_logs_path.sh
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_home_layout ../../nextui/home_layout.c test_home_layout.c -lm
/tmp/nx_test_home_layout
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_homeart_model ../../nextui/homeart_model.c test_homeart_model.c
/tmp/nx_test_homeart_model
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_home_stats_model ../../nextui/home_stats_model.c test_home_stats_model.c
/tmp/nx_test_home_stats_model
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_home_strip ../../nextui/home_strip.c ../../nextui/gameinfo_text.c test_home_strip.c
/tmp/nx_test_home_strip
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_grid_layout ../../nextui/grid_layout.c ../../nextui/row_model.c test_grid_layout.c -lm
/tmp/nx_test_grid_layout
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_row_model ../../nextui/row_model.c test_row_model.c -lm
/tmp/nx_test_row_model
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_area_scale ../../nextui/area_scale.c test_area_scale.c -lm
/tmp/nx_test_area_scale
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_placeholder_art ../../nextui/placeholder_art.c test_placeholder_art.c -lm
/tmp/nx_test_placeholder_art
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_stack_model ../../nextui/stack_model.c ../../nextui/row_model.c test_stack_model.c -lm
/tmp/nx_test_stack_model
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I../ui -o /tmp/nx_test_caption_fit ../../nextui/caption_fit.c ../../nextui/infoband_layout.c test_caption_fit.c
/tmp/nx_test_caption_fit
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_menustyle test_menustyle.c
/tmp/nx_test_menustyle
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I../ui -o /tmp/nx_test_menu_transition ../../nextui/menu_transition.c ../ui/ui_ease.c test_menu_transition.c -lm
/tmp/nx_test_menu_transition
