#!/usr/bin/env python3
"""Check OSD widget packing against trimui_osdd's 6-by-4 grid."""

import json
import sys
from pathlib import Path

GRID_WIDTH = 6
GRID_HEIGHT = 4
REQUIRED_TILES = (
    "com.trimui.osd.app.musicplayer",
    "com.trimui.osd.app.mastervolume",
    "com.trimui.osd.app.balance",
)
BRIGHTNESS_PACKAGE = "com.trimui.osd.slider.backlight"
COMPACT_BRIGHTNESS_PACKAGE = "com.trimui.osd.app.brightness"


def configs(root: Path, device: str) -> dict[str, dict]:
    paths = [
        root / "skeleton/SYSTEM/osd/common/widgets",
        root / f"skeleton/SYSTEM/osd/device/{device}/widgets",
    ]
    result: dict[str, dict] = {}
    for directory in paths:
        if not directory.is_dir():
            continue
        for config_path in directory.glob("*/config.json"):
            config = json.loads(config_path.read_text())
            result[config["package"]] = config
    return result


def check(root: Path, device: str) -> None:
    layout_path = root / f"skeleton/SYSTEM/osd/device/{device}/osdlayout.json"
    layout = json.loads(layout_path.read_text())
    widget_configs = configs(root, device)
    occupied = [None] * (GRID_WIDTH * GRID_HEIGHT)
    placements: dict[str, tuple[int, int]] = {}

    for entry in layout["widgetlist"]:
        package = entry["package"]
        config = widget_configs.get(package)
        if config is None:
            raise AssertionError(f"{device}: no config for {package}")
        width = config["gridwidth"]
        height = config["gridheight"]
        placement = None
        for slot in range(len(occupied)):
            x, y = slot % GRID_WIDTH, slot // GRID_WIDTH
            if x + width > GRID_WIDTH or y + height > GRID_HEIGHT:
                continue
            cells = [
                (y + row) * GRID_WIDTH + x + column
                for row in range(height)
                for column in range(width)
            ]
            if all(occupied[cell] is None for cell in cells):
                placement = (x, y)
                for cell in cells:
                    occupied[cell] = package
                break
        if placement is None:
            raise AssertionError(
                f"{device}: {package} does not fit in {GRID_WIDTH}x{GRID_HEIGHT}"
            )
        placements[package] = placement

    empty_cells = occupied.count(None)
    if empty_cells:
        raise AssertionError(f"{device}: {empty_cells} empty grid cells")

    for package in REQUIRED_TILES:
        tile = widget_configs[package]
        if (tile["gridwidth"], tile["gridheight"]) != (2, 1):
            raise AssertionError(f"{device}: {package} is not 2x1")
        if package not in placements:
            raise AssertionError(f"{device}: missing {package}")
    brightness = widget_configs[BRIGHTNESS_PACKAGE]
    if device == "smartpros":
        compact = widget_configs[COMPACT_BRIGHTNESS_PACKAGE]
        if (compact["gridwidth"], compact["gridheight"]) != (2, 1):
            raise AssertionError(f"{device}: compact brightness is not 2x1")
        if COMPACT_BRIGHTNESS_PACKAGE not in placements:
            raise AssertionError(f"{device}: missing compact brightness")
    elif (brightness["gridwidth"], brightness["gridheight"]) != (4, 1):
        raise AssertionError(f"{device}: stock brightness is not 4x1")
    if len(placements) != len(layout["widgetlist"]):
        raise AssertionError(f"{device}: duplicate or omitted widget package")
    print(
        f"{device}: {len(layout['widgetlist'])} widgets fit 6x4; "
        f"tiles at {[placements[package] for package in REQUIRED_TILES]}"
    )


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    for device in ("brick", "brickpro", "smartpro", "smartpros"):
        check(root, device)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, KeyError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from None
