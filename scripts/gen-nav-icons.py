#!/usr/bin/env python3
"""Generate per-scale nav button/trigger/dpad icons from the 128x128 masters.

The hint bar renders button glyphs at SCALE1(BUTTON_SIZE) = BUTTON_SIZE * FIXED_SCALE
pixels (BUTTON_SIZE = 16, see workspace/all/common/defines.h). FIXED_SCALE is 2 on
most devices and 3 on Brick, and on tg5040 it is chosen at *runtime*
(is_brick ? 3 : 2) -- so a single build must ship both sizes. We therefore bake one
variant per scale, named with the same "@Nx" convention as assets@Nx.png, so the
loader can pick nav_<name>@<FIXED_SCALE>x.png at runtime with zero runtime scaling.

Downscaling the full 128x128 canvas uniformly (LANCZOS) preserves the transparent
padding the artist baked in, so relative glyph proportions stay intact. Masters are
white alpha masks; output stays a white RGBA mask so the existing tint-on-blit path
(GFX_blitSurfaceColor) still applies theme colors.

Idempotent: safe to re-run. Usage:  python3 scripts/gen-nav-icons.py [--check]
"""
import glob
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")

# LANCZOS moved under Image.Resampling in Pillow >= 9.1
try:
    LANCZOS = Image.Resampling.LANCZOS
except AttributeError:  # older Pillow
    LANCZOS = Image.LANCZOS

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES_DIR = os.path.join(REPO, "skeleton", "SYSTEM", "res")

BUTTON_SIZE = 16            # logical px, must match defines.h
SCALES = (3, 2.25, 2.4375)  # the hint bar's CHROME_SCALEs (platform.h): the Brick, the 1280x720 panels, the Brick Pro
MASTER_PREFIX = "nav_"      # 128x128 masters (buttons, triggers, dpad)


def is_master(path):
    """A master is a source nav_* glyph, not one of our @Nx outputs.

    Matched case-insensitively so mis-cased masters (e.g. naV_button_l3.png)
    are still picked up -- outputs are always normalised to lowercase below.
    """
    name = os.path.basename(path).lower()
    return name.startswith(MASTER_PREFIX) and "@" not in name


def main():
    check_only = "--check" in sys.argv
    masters = [p for p in sorted(glob.glob(os.path.join(RES_DIR, "*.png")))
               if is_master(p)]
    if not masters:
        sys.exit(f"no masters matching {MASTER_GLOB} in {RES_DIR}")

    written, stale = [], []
    for src in masters:
        # Normalise output stem to lowercase (fixes the naV_button_l3 typo).
        stem = os.path.splitext(os.path.basename(src))[0].lower()
        img = Image.open(src).convert("RGBA")
        # Crop away the transparent padding baked into the 128x128 canvas so the
        # glyph fills its box, then scale so the *content* height == the button
        # height (BUTTON_SIZE * scale). Width follows the content's aspect ratio,
        # so disc glyphs come out ~square (matching the old drawn circle) while
        # wider label glyphs (START/SELECT) keep their proportions instead of
        # shrinking into a square. The runtime blits each glyph at its own size.
        bbox = img.getbbox()
        content = img.crop(bbox) if bbox else img
        for scale in SCALES:
            target_h = round(BUTTON_SIZE * scale)              # 48 @3x, 36 @2.25x, 39 @2.4375x
            ratio = target_h / content.height
            target_w = max(1, round(content.width * ratio))
            out = os.path.join(RES_DIR, f"{stem}@{scale:g}x.png")
            if check_only:
                if not os.path.exists(out):
                    stale.append(os.path.basename(out))
                continue
            content.resize((target_w, target_h), LANCZOS).save(out)
            written.append(f"{os.path.basename(out)} ({target_w}x{target_h})")

    if check_only:
        if stale:
            print("missing generated icons:\n  " + "\n  ".join(stale))
            sys.exit(1)
        print(f"ok: all @Nx icons present for {len(masters)} masters")
        return

    print(f"generated {len(written)} icons from {len(masters)} masters:")
    for w in written:
        print(f"  {w}")


if __name__ == "__main__":
    main()
