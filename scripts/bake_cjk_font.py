#!/usr/bin/env python3
"""Bake the CJK glyph atlas used by GameKit.CjkFont.

Scans the given .zan sources for non-ASCII characters and renders every
distinct codepoint into a fixed-cell white-on-transparent atlas
(cjk_font.png) plus a metadata file (cjk_font.txt):

    cell cols scale count
    cp1 cp2 cp3 ...

Codepoints are written sorted ascending so the runtime can binary-search.
Glyphs share one common baseline (from the font's ascent/descent metrics)
instead of being individually centered, so mixed text sits evenly.

Usage:
    python3 scripts/bake_cjk_font.py OUT_DIR SRC.zan [SRC2.zan ...]
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = [
    os.environ.get("ZAN_CJK_FONT", ""),
    "C:/Windows/Fonts/msyhbd.ttc",
    "C:/Windows/Fonts/msyh.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
]
CELL = 54        # design cell (layout units, square; ASCII GlyphFont cell is 47x57)
SCALE = 3        # atlas is baked at CELL*SCALE px per glyph
COLS = 32


def main():
    out_dir, srcs = sys.argv[1], sys.argv[2:]
    cps = set()
    for p in srcs:
        for ch in open(p, encoding="utf-8").read():
            if ord(ch) > 126:
                cps.add(ord(ch))
    cps = sorted(cps)
    if not cps:
        sys.exit("no non-ASCII codepoints found")

    font_path = next(p for p in FONT_CANDIDATES if os.path.exists(p))
    px = CELL * SCALE
    font = ImageFont.truetype(font_path, int(px * 0.82))
    ascent, descent = font.getmetrics()
    # one shared baseline: glyph box (ascent+descent) centered in the cell
    base_y = (px - (ascent + descent)) // 2
    rows = (len(cps) + COLS - 1) // COLS
    img = Image.new("RGBA", (COLS * px, rows * px), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i, cp in enumerate(cps):
        cx, cy = (i % COLS) * px, (i // COLS) * px
        ch = chr(cp)
        bbox = d.textbbox((0, 0), ch, font=font)
        w = bbox[2] - bbox[0]
        d.text((cx + (px - w) // 2 - bbox[0], cy + base_y), ch, font=font,
               fill=(255, 255, 255, 255))
    img.save(f"{out_dir}/cjk_font.png")
    with open(f"{out_dir}/cjk_font.txt", "w") as f:
        f.write(f"{CELL} {COLS} {SCALE} {len(cps)}\n")
        f.write(" ".join(map(str, cps)) + "\n")
    print(f"baked {len(cps)} glyphs -> {out_dir}/cjk_font.png")


if __name__ == "__main__":
    main()
