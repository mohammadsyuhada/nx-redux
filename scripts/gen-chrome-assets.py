#!/usr/bin/env python3
"""Bake the fractional asset sheets (assets@2.25x, @2.4375x, @1.5x and @1.625x.png) from the assets@4x.png master.

The hint bar, page titles and status group draw at CHROME_SCALE (the platform.h files): 3x on the Brick, where
assets@3x.png serves, 2.4375x on the Brick Pro and 2.25x on the 1280x720 panels, which no hand-made sheet covers. The
volume / brightness indicator draws at two thirds of that (INDICATOR_SCALE, defines.h): 2x on the Brick (assets@2x.png),
1.625x and 1.5x elsewhere. This area-scales the 512 px @4x sheet to each size (premultiplied, so transparent edges don't darken);
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
SCALES = (2.25, 2.4375, 1.5, 1.625)  # CHROME_SCALE on the 1280x720 panels and the Brick Pro, and their INDICATOR_SCALE


def main():
    src = Image.open(SRC).convert("RGBA")
    for scale in SCALES:
        size = round(src.width * scale / 4)
        out = os.path.join(RES_DIR, f"assets@{scale:g}x.png")
        src.convert("RGBa").resize((size, size), Image.BOX).convert("RGBA").save(out, optimize=True)
        print(f"{os.path.basename(out)} ({size}x{size})")


if __name__ == "__main__":
    main()
