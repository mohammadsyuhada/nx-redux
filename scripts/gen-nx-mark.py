#!/usr/bin/env python3
# Regenerate skeleton/SYSTEM/res/menu/nx_mark.png (the Home tab's NX mark) from
# skeleton/SYSTEM/res/icon.svg: its two outline paths, stroked white (13.6, round
# joins) on transparent at 512 px in the icon's own coordinates. PIL draws them at
# 4x and downsamples with LANCZOS, so no SVG renderer is needed. The result is
# cropped to its ink (alpha bounding box), so whoever fits it into a box scales the
# strokes themselves, not the icon's padding. Keep PATHS/STROKE in sync with icon.svg.
# Usage: python3 scripts/gen-nx-mark.py   (from the repo root)
import os
from PIL import Image, ImageDraw

SIZE = 512
SS = 4
STROKE = 13.6
# M110.3 115.7 H147.7 L356.8 328.2 V183.8 L147.7 396.3 H110.3 Z
# M401.7 115.7 H364.3 L155.2 328.2 V183.8 L364.3 396.3 H401.7 Z
PATHS = [
    [(110.3, 115.7), (147.7, 115.7), (356.8, 328.2), (356.8, 183.8), (147.7, 396.3), (110.3, 396.3)],
    [(401.7, 115.7), (364.3, 115.7), (155.2, 328.2), (155.2, 183.8), (364.3, 396.3), (401.7, 396.3)],
]
WHITE = (255, 255, 255, 255)

def main():
    im = Image.new("RGBA", (SIZE * SS, SIZE * SS), (255, 255, 255, 0))
    d = ImageDraw.Draw(im)
    w = STROKE * SS
    r = w / 2
    for pts in PATHS:
        p = [(x * SS, y * SS) for x, y in pts]
        # closed outline; repeat the first segment so the closing corner is joined too
        d.line(p + [p[0], p[1]], fill=WHITE, width=round(w), joint="curve")
        for x, y in p:  # round joins at every vertex
            d.ellipse([x - r, y - r, x + r, y + r], fill=WHITE)
    out = im.resize((SIZE, SIZE), Image.LANCZOS)
    out = out.crop(out.getchannel("A").getbbox())  # tight: no transparent margin
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    path = os.path.join(root, "skeleton", "SYSTEM", "res", "menu", "nx_mark.png")
    out.save(path, optimize=True)
    print(path, out.size, out.getchannel("A").getbbox())

if __name__ == "__main__":
    main()
