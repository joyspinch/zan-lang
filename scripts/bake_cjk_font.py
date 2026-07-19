#!/usr/bin/env python3
"""Bake the CJK glyph atlas used by GameKit.CjkFont.

Scans the given .zan sources for non-ASCII characters and renders every
distinct codepoint into a fixed-cell white-on-transparent atlas
(cjk_font.png) plus a metadata file (cjk_font.txt):

    cell cols scale count
    cp1 cp2 cp3 ...

Codepoints are written sorted ascending so the runtime can binary-search.

Usage:
    python3 scripts/bake_cjk_font.py OUT_DIR SRC.zan [SRC2.zan ...]
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "C:/Windows/Fonts/msyh.ttc",
]
CELL = 54        # design cell (layout units, square; ASCII GlyphFont cell is 47x57)
SCALE = 2        # atlas is baked at CELL*SCALE px per glyph
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

    font_path = next(p for p in FONT_CANDIDATES if __import__("os").path.exists(p))
    px = CELL * SCALE
    font = ImageFont.truetype(font_path, int(px * 0.86))
    rows = (len(cps) + COLS - 1) // COLS
    img = Image.new("RGBA", (COLS * px, rows * px), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i, cp in enumerate(cps):
        cx, cy = (i % COLS) * px, (i // COLS) * px
        ch = chr(cp)
        bbox = d.textbbox((0, 0), ch, font=font)
        w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
        d.text((cx + (px - w) // 2 - bbox[0], cy + (px - h) // 2 - bbox[1]),
               ch, font=font, fill=(255, 255, 255, 255))
    img.save(f"{out_dir}/cjk_font.png")
    with open(f"{out_dir}/cjk_font.txt", "w") as f:
        f.write(f"{CELL} {COLS} {SCALE} {len(cps)}\n")
        f.write(" ".join(map(str, cps)) + "\n")
    print(f"baked {len(cps)} glyphs -> {out_dir}/cjk_font.png")


if __name__ == "__main__":
    main()
