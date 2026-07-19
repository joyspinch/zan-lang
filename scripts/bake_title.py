#!/usr/bin/env python3
"""Bake a glossy art-text game title banner (transparent PNG).

Renders CJK/ASCII text with a green-gold arcade logo look: drop shadow,
thick dark outline, vertical gradient fill and a top highlight, then trims
and pads to a 3:1 banner so games can draw it at a fixed aspect.

Usage: python scripts/bake_title.py OUT.png TITLE_TEXT [W H]
"""
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

FONT_CANDIDATES = [
    "C:/Windows/Fonts/msyhbd.ttc",
    "C:/Windows/Fonts/msyh.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
]

def main():
    out, text = sys.argv[1], sys.argv[2]
    bw = int(sys.argv[3]) if len(sys.argv) > 3 else 1152
    bh = int(sys.argv[4]) if len(sys.argv) > 4 else 384
    import os
    font_path = next(p for p in FONT_CANDIDATES if os.path.exists(p))
    size = 420
    font = ImageFont.truetype(font_path, size)
    pad = 120
    d0 = ImageDraw.Draw(Image.new("RGBA", (8, 8)))
    box = d0.textbbox((0, 0), text, font=font, stroke_width=22)
    w, h = box[2] - box[0], box[3] - box[1]
    img = Image.new("RGBA", (w + pad * 2, h + pad * 2), (0, 0, 0, 0))
    ox, oy = pad - box[0], pad - box[1]

    # soft drop shadow
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).text((ox, oy + 18), text, font=font, fill=(10, 40, 16, 170),
                            stroke_width=22, stroke_fill=(10, 40, 16, 170))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(10)))

    # dark outline
    ImageDraw.Draw(img).text((ox, oy), text, font=font, fill=(24, 84, 38, 255),
                             stroke_width=22, stroke_fill=(24, 84, 38, 255))
    # gradient fill masked by the glyphs
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).text((ox, oy), text, font=font, fill=255)
    grad = Image.new("RGBA", img.size)
    top, bot = (222, 255, 120), (52, 168, 74)
    gh = img.size[1]
    gp = grad.load()
    for y in range(gh):
        t = y / max(1, gh - 1)
        c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,)
        for x in range(img.size[0]):
            gp[x, y] = c
    img.paste(grad, (0, 0), mask)
    # top gloss highlight inside the glyphs
    gloss = Image.new("L", img.size, 0)
    ImageDraw.Draw(gloss).text((ox, oy), text, font=font, fill=110)
    gcut = Image.new("L", img.size, 0)
    ImageDraw.Draw(gcut).rectangle([0, 0, img.size[0], oy + h * 2 // 5], fill=255)
    from PIL import ImageChops
    gloss = ImageChops.multiply(gloss, gcut).filter(ImageFilter.GaussianBlur(3))
    img.alpha_composite(Image.merge("RGBA", [Image.new("L", img.size, 255)] * 3 + [gloss]))

    bb = img.getbbox()
    img = img.crop(bb)
    w, h = img.size
    cw, ch = (w, (w + 2) // 3) if w >= h * 3 else (h * 3, h)
    canvas = Image.new("RGBA", (cw, ch), (0, 0, 0, 0))
    canvas.paste(img, ((cw - w) // 2, (ch - h) // 2))
    canvas.resize((bw, bh), Image.LANCZOS).save(out)
    print(f"baked title -> {out}")


if __name__ == "__main__":
    main()
