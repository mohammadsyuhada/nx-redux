#!/usr/bin/env python3
# Regenerate the NX Redux logo assets from skeleton/SYSTEM/res/icon.svg.
# The mark is two round-joined stroked polygons (optionally on the icon's
# rounded square); PIL draws them at 8x and downsamples, so no SVG renderer
# is needed. Keep PATHS/STROKE in sync with icon.svg.
# Usage: python3 scripts/gen-logo-assets.py   (from the repo root)
import os
from PIL import Image, ImageDraw
SS = 8
STROKE = 13.6
PATHS = [
    [(110.3,115.7),(147.7,115.7),(356.8,328.2),(356.8,183.8),(147.7,396.3),(110.3,396.3)],
    [(401.7,115.7),(364.3,115.7),(155.2,328.2),(155.2,183.8),(364.3,396.3),(401.7,396.3)],
]
# mark bounds including the stroke
H = STROKE/2
BX0, BY0, BX1, BY1 = 110.3-H, 115.7-H, 401.7+H, 396.3+H

def draw_mark(d, s, ox, oy, color):
    w = STROKE*s
    r = w/2
    for pts in PATHS:
        p = [(ox+x*s, oy+y*s) for x,y in pts]
        d.line(p + [p[0], p[1]], fill=color, width=round(w), joint="curve")
        for x,y in p:
            d.ellipse([x-r, y-r, x+r, y+r], fill=color)

def mark(size, fit_w, bg, color, mode):
    """Mark centred in size=(W,H), scaled so its stroked width is fit_w px."""
    W, Hh = size
    s = fit_w/(BX1-BX0)
    im = Image.new(mode, (W*SS, Hh*SS), bg)
    d = ImageDraw.Draw(im)
    mw, mh = (BX1-BX0)*s, (BY1-BY0)*s
    ox = (W-mw)/2 - BX0*s
    oy = (Hh-mh)/2 - BY0*s
    draw_mark(d, s*SS, ox*SS, oy*SS, color)
    return im.resize(size, Image.LANCZOS)

def icon(px):
    s = px/512
    im = Image.new("RGBA", (px*SS, px*SS), (0,0,0,0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0,0,px*SS-1,px*SS-1], radius=120*s*SS, fill=(0x1F,0x1F,0x1F,255))
    draw_mark(d, s*SS, 0, 0, (255,255,255,255))
    return im.resize((px,px), Image.LANCZOS)

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

def out(rel):
    return os.path.join(ROOT, rel)

# install/update splash (boot.sh via show2): white mark on transparent, tight
# crop with an 8-unit pad, 512 px wide; show2 scales it to --logoheight
pad = 8
lw = 512
lh = round((BY1-BY0+2*pad) * lw/(BX1-BX0+2*pad))
logo = mark((lw, lh), lw*(BX1-BX0)/(BX1-BX0+2*pad), (0,0,0,0), (255,255,255,255), "RGBA")
for rel in ("skeleton/SYSTEM/res/logo.png", "workspace/tg5040/install/logo.png", "workspace/tg5050/install/logo.png"):
    logo.save(out(rel), optimize=True)

# default boot logos (Settings > Boot logo): white on black, 24-bit BMP
brick = mark((216,235), 208, (0,0,0), (255,255,255), "RGB")
smartpro = mark((128,128), 120, (0,0,0), (255,255,255), "RGB")
for plat in ("tg5040", "tg5050"):
    base = f"skeleton/SYSTEM/{plat}/paks/Tools/Settings.pak/bootlogo"
    brick.save(out(f"{base}/brick/bootlogo.bmp"))
    smartpro.save(out(f"{base}/smartpro/bootlogo.bmp"))

# desktop app icon (AppImage / macOS .icns): the full icon.svg
icon(1024).save(out("scripts/desktop/nxredux-icon.png"), optimize=True)
