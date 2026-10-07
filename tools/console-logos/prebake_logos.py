#!/usr/bin/env python3
"""Pre-bakes the main menu's console logos (res/menu/menu_logo_*.png) at the largest size the launcher ever draws them,
so the art loader's thread decodes a ~650 px image instead of a 1024 px one and the Consoles Carousel's GPU sprites draw
the file as it is (no halving at runtime: MenuArt_loadLevel).

Every logo is one colour (the menu's off-white, 0xE0E0E0) over a transparent ground, its shape all in the alpha. So
only the alpha is resampled (area-averaged, as the launcher's AreaScale_argb: no ringing, no darkened edges), and the
result is written as an 8-bit palette PNG whose 256 entries are that colour at alpha 0..255 (tRNS): lossless, and about
half the size of the RGBA file.

The largest box each consumer asks MenuArt for (px), measured on the devices (FPROF logging of MenuArt_load,
2026-10-07) at both UI scales (Small 2x, Large 3x) of both screens:

  Consoles vertical stack, selected    653 x 199   Smart Pro S / Brick Pro 1280x720 (Brick 1024x768: 523 x 159)
  Consoles Carousel, selected slot     559 x 221   1280x720 (Brick: 491 x 194); both by the screen, not the UI scale
  List page background logo            512 x 334   1280x720, 40% of the width x 60% of the rows (Brick: 409 x 320)
  Grid console tile                    371 x 149   1280x720 at Large (Brick Large: 359 x 143)

A logo's target width is the largest of min(box_w, box_h x its aspect) over those boxes, rounded up to even; a logo is
never enlarged. Re-running is harmless: a logo within 2 px of its target is only re-encoded.

Usage: prebake_logos.py <repo root>    (needs Pillow)
"""
import glob, math, os, sys
from PIL import Image

BOXES = [(653, 199), (559, 221), (512, 334), (371, 149)]
TINT = (0xE0, 0xE0, 0xE0)


def target_size(w, h):
    aspect = w / h
    tw = max(min(bw, bh * aspect) for bw, bh in BOXES)
    tw = min(w, 2 * math.ceil(tw / 2))
    if w - tw <= 2:  # already baked (a re-run): its own rounding, not a pixel or two off it
        return w, h
    th = min(h, max(1, round(tw / aspect)))
    return tw, th


def write_palette_png(alpha, path):
    """alpha: an L image. One colour at 256 alpha levels: index i = alpha i."""
    im = alpha.copy()
    im = im.convert("P")  # an L image's values become the indices as they are
    im.putdata(list(alpha.getdata()))
    im.putpalette(list(TINT) * 256)
    tmp = path + ".tmp"
    im.save(tmp, format="PNG", optimize=True, transparency=bytes(range(256)))
    os.replace(tmp, path)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    menu = os.path.join(sys.argv[1], "skeleton", "SYSTEM", "res", "menu")
    before = after = 0
    for path in sorted(glob.glob(os.path.join(menu, "menu_logo_*.png"))):
        before += os.path.getsize(path)
        src = Image.open(path).convert("RGBA")
        rgb = {p[:3] for p in src.getdata() if p[3]}
        if rgb - {TINT}:
            sys.exit(f"{path}: not one {TINT} colour over alpha ({len(rgb)} colours); re-tint it first")
        tw, th = target_size(*src.size)
        alpha = src.getchannel("A")
        if (tw, th) != src.size:
            alpha = alpha.resize((tw, th), Image.BOX)
        write_palette_png(alpha, path)
        after += os.path.getsize(path)
        print(f"{os.path.basename(path):28s} {src.size[0]:4d}x{src.size[1]:<4d} -> {tw:4d}x{th:<4d}")
    print(f"logos: {before / 1024:.0f} KB -> {after / 1024:.0f} KB")


if __name__ == "__main__":
    main()
