#!/usr/bin/env bash
set -euo pipefail
python3 - <<'PY'
from pathlib import Path
import re

menu = Path("workspace/all/musicplayer/module_menu.c").read_text()
player = Path("workspace/all/musicplayer/module_player.c").read_text()
assert "MusicClient_update();\n\t\tModuleCommon_setAutosleepDisabled(Background_isPlaying());" in menu
assert len(re.findall(r"MusicClient_update\(\);\n\s*ModuleCommon_setAutosleepDisabled\(Background_isPlaying\(\)\);", player)) >= 2
print("music autosleep refresh checks passed")
PY
