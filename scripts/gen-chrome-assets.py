#!/usr/bin/env python3
"""Bake the fractional asset sheets (assets@2.25x, @2.5x, @1.5x and @1.6875x.png) from the assets@4x.png master.

The UI draws at each device's UI scale (ui_scale.h): 3x on the Brick, where assets@3x.png serves, 2.5x on the Brick
Pro and 2.25x on the 1280x720 panels, which no hand-made sheet covers. The volume / brightness indicator draws at two
thirds of that (INDICATOR_SCALE, defines.h): 2x on the Brick (assets@2x.png), 1.6875x and 1.5x elsewhere. After tuning a
device's scale, update DEVICE_SCALES here and in gen-nav-icons.py and re-run both. This area-scales the 512 px @4x sheet to each size (premultiplied, so transparent edges don't darken);
GFX_initAssetRectsInto rounds the same unit rects at that scale to find each sprite in it.

Idempotent: safe to re-run. Usage:  python3 scripts/gen-chrome-assets.py
"""
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES_DIR = os.path.join(REPO, "skeleton", "SYSTEM", "res")
SRC = os.path.join(REPO, "scripts", "assets", "assets@4x.png")  # the master; never loaded on a device, so not shipped
# Each device's UI scale (ui_scale.h UIScale_forDevice) and its volume indicator's two thirds (INDICATOR_SCALE,
# defines.h), on 1/16; 2x and 3x are hand-made sheets.
DEVICE_SCALES = (3.0, 2.5, 2.25)
SCALES = tuple(sorted({s for d in DEVICE_SCALES for s in (d, round(d * 2 / 3 * 16) / 16)} - {2.0, 3.0}))


def main():
    src = Image.open(SRC).convert("RGBA")
    for scale in SCALES:
        size = round(src.width * scale / 4)
        out = os.path.join(RES_DIR, f"assets@{scale:g}x.png")
        src.convert("RGBa").resize((size, size), Image.BOX).convert("RGBA").save(out, optimize=True)
        print(f"{os.path.basename(out)} ({size}x{size})")


if __name__ == "__main__":
    main()
