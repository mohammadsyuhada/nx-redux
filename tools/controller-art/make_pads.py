#!/usr/bin/env python3
"""Controller art for the main menu's Consoles tab, from a folder of 1000 px transparent PNGs (ScreenScraper).

Cuts a cable leaving any edge (the narrow solid run reaching the border, followed inward until the body) and crops to
what is drawn (as nx-mobile's tools/controller-art/make_pads.py), then bakes the looks the launcher can't do at
runtime (no rotation, no colour matrix, software blits only):

  res/menu/menu_pad_<id>.png                 the Carousels': long side 640, saturate(0.55), RGB x 0.32 k
  res/menu/menu_pad_<id>_list_<W>x<H>.png    the List's, placed for that screen: fitted in 74% W x 100% H,
                                             right-aligned, moved 16% W right and 2% H down, rotated -12 deg about
                                             the box centre, RGB x 0.55 k, alpha x 0.35, cropped to the screen
  workspace/all/nextui/controller_art_table.h  id, k, the Carousel image's aspect, the List images' positions

k is the brightness factor of docs/controller-art.md (the PlayStation pad's mean luminance over the image's own,
clamped 0.5..2.0, three set by eye); the table there is kept as it is (K), and the measured value is printed beside it.
Needs Pillow; pngquant, when on the PATH, then shrinks each PNG to 256 colours with alpha (no dither: the same by eye at
these brightnesses, about 40% smaller). Usage: make_pads.py <source dir> <repo root>
"""
import os, shutil, subprocess, sys
from PIL import Image, ImageStat

SOURCES = {   # console id -> source file name (without .png)
    "nds": "nintendo-ds", "n64": "nintendo-64", "psp": "sony-psp", "ps": "playstation", "gb": "game-boy",
    "gbc": "game-boy-color", "gba": "game-boy-advance", "fc": "nintendo-es", "sfc": "super-nintendo-es",
    "md": "sega-mega-drive", "sms": "sega-master-system", "gg": "sega-game-gear", "sg1000": "sega-sg-1000",
    "fds": "famicom-disk-system", "ngpc": "neo-geo-pocket", "pce": "pc-engine", "vb": "virtual-boy",
    "lynx": "atari-lynx", "a2600": "atari-2600", "a5200": "atari-5200", "a7800": "atari-7800",
    "pkm": "pokemon-mini", "wsc": "wonderswan-color", "coleco": "colecovision", "dc": "dreamcast",
    "vic20": "commodore-vic-20", "c64": "competition-pro",   # the Competition Pro also covers C128, Plus/4 and Amiga
}
K = {   # docs/controller-art.md's table (measured, gba/fc/psp set by eye); an id missing here takes its measured k
    "ps": 1.00, "psp": 1.50, "nds": 0.65, "n64": 0.79, "gb": 0.77, "gbc": 1.26, "gba": 1.15, "fc": 0.80,
    "sfc": 0.71, "md": 1.80, "sms": 2.00, "gg": 2.00, "sg1000": 1.06, "fds": 1.12, "ngpc": 0.89, "pkm": 1.18,
    "coleco": 2.00, "pce": 0.75, "vb": 2.00, "lynx": 2.00, "a2600": 2.00, "a5200": 2.00, "a7800": 1.12,
    "wsc": 2.00, "dc": 0.64,
    "c64": 1.00,  # by eye: at its measured 1.55 the red stick and buttons read bolder than the grey pads
}
# (Carousel, List) saturation where CAROUSEL_SATURATION / LIST_SATURATION leave a colour too loud (by eye)
SATURATION = {"c64": (0.25, 0.45)}
SCREENS = [(1024, 768), (1280, 720)]
CAROUSEL_LONG = 640
CAROUSEL_SATURATION, CAROUSEL_BRIGHTNESS = 0.55, 0.32
LIST_SATURATION, LIST_BRIGHTNESS, LIST_ALPHA = 1.0, 0.55, 0.35
LIST_BOX_W, LIST_BOX_H, LIST_PUSH_X, LIST_PUSH_Y, LIST_ANGLE = 0.74, 1.0, 0.16, 0.02, 12  # Pillow: CCW = CSS -12deg

CABLE_MAX = 130   # a solid run through the cable column narrower than this is cable (or its strain relief), not body
SOLID = 120       # alpha above this is drawn body; below it, soft shadow (which can bridge a cable to the body)
SWING = 90        # cleared each side of the cable's column above the body (a coil swings off it)
ROTATE = {"top": None, "bottom": Image.Transpose.ROTATE_180, "left": Image.Transpose.ROTATE_270, "right": Image.Transpose.ROTATE_90}
UNROTATE = {"top": None, "bottom": Image.Transpose.ROTATE_180, "left": Image.Transpose.ROTATE_90, "right": Image.Transpose.ROTATE_270}

def cut_top_cable(im):
    """A cable leaving the top edge: find where the body starts on the cable's column (the first row whose solid run
    through it is body-wide; soft shadow does not count), then clear everything above it in a window around the
    cable's column (a coiled cable swings off it)."""
    px = im.load(); w, h = im.size
    top = [x for x in range(w) if px[x, 3][3] > 128]
    if not top or max(top) - min(top) > CABLE_MAX: return False
    c0, c1 = min(top), max(top)
    cx = (c0 + c1) // 2
    body = h
    for y in range(h):
        if px[cx, y][3] <= SOLID: continue
        l = r = cx
        while l > 0 and px[l - 1, y][3] > SOLID: l -= 1
        while r < w - 1 and px[r + 1, y][3] > SOLID: r += 1
        if r - l > CABLE_MAX: body = y; break
    for y in range(body):
        for x in range(max(0, c0 - SWING), min(w, c1 + SWING + 1)): px[x, y] = (0, 0, 0, 0)
        for x in range(w):   # the cable's faint shadow above the body
            if px[x, y][3] < 90: px[x, y] = (0, 0, 0, 0)
    return True

def clean(path):
    """The source with its cable cut, cropped to what is drawn, at full resolution."""
    im = Image.open(path).convert("RGBA")
    for edge in ("top", "bottom", "left", "right"):
        t = im.transpose(ROTATE[edge]) if ROTATE[edge] is not None else im
        if cut_top_cable(t):
            im = t.transpose(UNROTATE[edge]) if UNROTATE[edge] is not None else t
            print(f"  cable cut from the {edge}", file=sys.stderr)
    bb = im.getchannel("A").point(lambda v: 255 if v > 10 else 0).getbbox()
    return im.crop(bb)

def luminance(im):
    mask = im.getchannel("A").point(lambda v: 255 if v > 200 else 0)
    return ImageStat.Stat(im.convert("L"), mask).mean[0]

def colour(im, saturation, brightness):
    """CSS saturate(s) (luminance weights 0.213 / 0.715 / 0.072), then each RGB row times the brightness; alpha kept."""
    s = saturation
    m = [[0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s],
         [0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s],
         [0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s]]
    r, g, b, a = im.split()
    rgb = Image.merge("RGB", (r, g, b)).convert("RGB", tuple(v * brightness for row in m for v in row + [0]))
    return Image.merge("RGBA", (*rgb.split(), a))

def carousel(im, k, saturation=CAROUSEL_SATURATION):
    t = im.copy(); t.thumbnail((CAROUSEL_LONG, CAROUSEL_LONG), Image.LANCZOS)
    return colour(t, saturation, CAROUSEL_BRIGHTNESS * k)

def list_image(im, k, sw, sh, saturation=LIST_SATURATION):
    """The List's pad as it lands on a sw x sh screen: (image cropped to the screen and its drawn pixels, x, y)."""
    bw, bh = LIST_BOX_W * sw, LIST_BOX_H * sh
    s = min(bw / im.width, bh / im.height)
    fw, fh = max(1, round(im.width * s)), max(1, round(im.height * s))
    fitted = colour(im.resize((fw, fh), Image.LANCZOS), saturation, LIST_BRIGHTNESS * k)
    a = fitted.getchannel("A").point(lambda v: round(v * LIST_ALPHA))
    fitted.putalpha(a)
    # the box: right-aligned, then pushed; the image centred in it, rotated about the box (= image) centre
    cx = sw - bw / 2 + LIST_PUSH_X * sw
    cy = sh / 2 + LIST_PUSH_Y * sh
    rot = fitted.rotate(LIST_ANGLE, resample=Image.BICUBIC, expand=True)
    x0, y0 = round(cx - rot.width / 2), round(cy - rot.height / 2)
    canvas = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    canvas.alpha_composite(rot, (max(0, x0), max(0, y0)), (max(0, -x0), max(0, -y0)))
    bb = canvas.getchannel("A").point(lambda v: 255 if v > 0 else 0).getbbox()
    return canvas.crop(bb), bb[0], bb[1]

def save(im, out):
    im.save(out, optimize=True)
    if shutil.which("pngquant"):
        subprocess.run(["pngquant", "256", "--speed", "1", "--nofs", "--strip", "--force", "--output", out, out], check=True)
    return os.path.getsize(out)

def main(src, root):
    menu = os.path.join(root, "skeleton", "SYSTEM", "res", "menu")
    table = os.path.join(root, "workspace", "all", "nextui", "controller_art_table.h")
    imgs = {}
    for cid in sorted(SOURCES):
        print(cid, file=sys.stderr)
        imgs[cid] = clean(os.path.join(src, SOURCES[cid] + ".png"))
    def measure(im):
        t = im.copy(); t.thumbnail((CAROUSEL_LONG, CAROUSEL_LONG), Image.LANCZOS)
        return luminance(t)
    ref = measure(imgs["ps"])
    rows, total = [], 0
    for cid in sorted(SOURCES):
        im = imgs[cid]
        lum = measure(im)
        k = K.get(cid, round(min(2.0, max(0.5, ref / lum)), 2))
        sat_c, sat_l = SATURATION.get(cid, (CAROUSEL_SATURATION, LIST_SATURATION))
        c = carousel(im, k, sat_c)
        out = os.path.join(menu, f"menu_pad_{cid}.png")
        total += save(c, out)
        pos = []
        for sw, sh in SCREENS:
            l, x, y = list_image(im, k, sw, sh, sat_l)
            out = os.path.join(menu, f"menu_pad_{cid}_list_{sw}x{sh}.png")
            total += save(l, out)
            pos += [x, y]
        rows.append((cid, k, c.width / c.height, pos, lum))
    for cid, k, _, _, lum in rows:
        print(f"{cid:7s} k {k:.2f}  measured {min(2.0, max(0.5, ref / lum)):.2f}", file=sys.stderr)
    print(f"total {total / 1e6:.1f} MB", file=sys.stderr)
    with open(table, "w") as f:
        f.write("// Generated by tools/controller-art/make_pads.py; do not edit. The controller art of the Consoles tab:\n")
        f.write("// per console id, its brightness factor k (baked into the images), the Carousel image's aspect (w/h) and\n")
        f.write("// the List image's top-left on a 1024x768 and a 1280x720 screen. Included by controller_art_model.c only\n")
        f.write("// (PadTableRow is in controller_art_model.h).\n")
        f.write("#ifndef CONTROLLER_ART_TABLE_H\n#define CONTROLLER_ART_TABLE_H\n\n")
        f.write("static const PadTableRow pad_table[] = {\n")
        for cid, k, asp, pos, _ in rows:
            f.write(f'\t{{"{cid}", {k:.2f}f, {asp:.4f}f, {pos[0]}, {pos[1]}, {pos[2]}, {pos[3]}}},\n')
        f.write("};\n\n#endif\n")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
