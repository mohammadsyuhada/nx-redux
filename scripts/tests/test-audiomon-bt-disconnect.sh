#!/usr/bin/env bash
set -euo pipefail
python3 - <<'PY'
from pathlib import Path

source = Path("workspace/all/audiomon/audiomon.c").read_text()
for name in ("handle_bt_disconnected", "handle_interfaces_removed"):
    start = source.index(f"static void {name}")
    body = source[start:source.index("\n}\n", start) + 2]
    assert body.index("current_route = ROUTED_DEFAULT;") < body.index("publish_sink_state();"), name
print("audiomon Bluetooth disconnect routing checks passed")
PY
