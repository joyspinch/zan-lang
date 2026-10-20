#!/usr/bin/env python3
"""gen_shared_art.py — 生成共享层的程序化素材（纹理 + UI 底板）。

共享层 = assets/game/：跨模板通用的可平铺纹理与 UI 底板。
机制类共享走 packages/，这里只管"换个游戏还成立"的通用美术资源。

确定性：固定随机种子，重跑字节一致（不依赖 API、不花时间）。
规则改动请改这个脚本，不要手改生成出来的图。

    python scripts/gen_shared_art.py
    python scripts/gen_shared_art.py --only textures/spark.png
"""

import argparse
import json
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "game")
PALETTE_FILE = os.path.join(OUT, "palette.json")


def palette():
    with open(PALETTE_FILE, "r", encoding="utf-8") as f:
        return json.load(f)["colors"]


def rgb(hex_color):
    h = hex_color.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


# ---------------------------------------------------------------- textures

def gen_paper(size=512, seed=11):
    """纸纹：细纤维 + 低频噪点，暖白底，可平铺。"""
    rnd = random.Random(seed)
    base = mix(rgb("#efe7d8"), rgb("#ffffff"), 0.45)
    im = Image.new("RGB", (size, size), base)
    d = ImageDraw.Draw(im)
    # 低频色斑（保证不是纯色块）
    for _ in range(26):
        cx, cy = rnd.randrange(size), rnd.randrange(size)
        r = rnd.randrange(size // 6, size // 3)
        tone = mix(base, rgb("#d8cdb6"), rnd.uniform(0.05, 0.16))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=tone)
    # 纤维
    for _ in range(2600):
        x, y = rnd.randrange(size), rnd.randrange(size)
        ln = rnd.randrange(4, 18)
        ang = rnd.uniform(-0.35, 0.35)
        tone = mix(base, rgb("#cbbfa6"), rnd.uniform(0.10, 0.30))
        d.line([x, y, x + ln * math.cos(ang), y + ln * math.sin(ang)], fill=tone)
    # 颗粒
    px = im.load()
    for _ in range(size * 12):
        x, y = rnd.randrange(size), rnd.randrange(size)
        px[x, y] = mix(px[x, y], rgb("#ffffff"), 0.35)
    return im.filter(ImageFilter.SMOOTH)


def gen_wood(size=512, seed=23):
    """木纹：沿 x 的年轮条纹，可平铺。"""
    rnd = random.Random(seed)
    dark = rgb("#6b4526")
    light = rgb("#a9713d")
    im = Image.new("RGB", (size, size), light)
    d = ImageDraw.Draw(im)
    for y in range(size):
        wave = math.sin(y * 0.055) * 9 + math.sin(y * 0.013 + 1.7) * 16
        t = 0.5 + 0.5 * math.sin((y + wave) * 0.09)
        d.line([0, y, size, y], fill=mix(dark, light, 0.25 + 0.75 * t))
    for _ in range(70):
        x0 = rnd.randrange(-40, size)
        y0 = rnd.randrange(size)
        w = rnd.randrange(2, 7)
        d.line([x0, y0, x0 + rnd.randrange(30, 160), y0 + rnd.randrange(-3, 4)],
               fill=mix(dark, rgb("#3d2716"), rnd.uniform(0.15, 0.4)), width=w)
    return im.filter(ImageFilter.SMOOTH)


def gen_metal(size=512, seed=37):
    """拉丝金属：横向细纹 + 竖向渐变高光。"""
    rnd = random.Random(seed)
    base = rgb("#8d949c")
    im = Image.new("RGB", (size, size), base)
    d = ImageDraw.Draw(im)
    for y in range(size):
        t = 0.35 + 0.65 * (1 - abs(y - size / 2) / (size / 2))
        d.line([0, y, size, y], fill=mix(rgb("#4a5058"), rgb("#d3dae1"), t * 0.85))
    for _ in range(3200):
        y = rnd.randrange(size)
        x = rnd.randrange(size)
        ln = rnd.randrange(10, 90)
        tone = mix(base, rgb("#eef3f8") if rnd.random() < 0.5 else rgb("#3b4046"),
                   rnd.uniform(0.06, 0.22))
        d.line([x, y, x + ln, y], fill=tone)
    return im.filter(ImageFilter.SMOOTH)


def gen_noise(size=256, seed=53):
    """半透明噪点：叠在纯色上破色块（Alpha 通道）。"""
    rnd = random.Random(seed)
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = im.load()
    for y in range(size):
        for x in range(size):
            v = rnd.randrange(0, 256)
            px[x, y] = (255, 255, 255, v) if v > 128 else (0, 0, 0, 255 - v)
    return im.filter(ImageFilter.GaussianBlur(0.4))


def gen_spark(size=128, seed=71):
    """粒子光点：径向柔光球（白心 + 暖边），带 Alpha。"""
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = im.load()
    c = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            dist = math.hypot(x - c, y - c) / c
            if dist > 1.0:
                continue
            a = (1.0 - dist) ** 2.4
            core = max(0.0, 1.0 - dist * 3.2)
            col = mix(rgb("#fff6d8"), rgb("#ffd27a"), min(1.0, dist * 1.3))
            px[x, y] = (col[0], col[1], col[2], int(255 * min(1.0, a * 0.85 + core)))
    return im.filter(ImageFilter.GaussianBlur(1.2))


def gen_vignette(size=512, seed=89):
    """暗角：中心透明、四周压暗（Alpha 通道）。"""
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = im.load()
    c = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            dist = math.hypot((x - c) / c, (y - c) / c)
            a = 0.0 if dist < 0.55 else min(1.0, (dist - 0.55) / 0.75) ** 1.8
            px[x, y] = (4, 6, 10, int(170 * a))
    return im.filter(ImageFilter.GaussianBlur(3.0))


# ---------------------------------------------------------------- ui

def gen_panel(size=96, seed=101):
    """九宫格面板底：中间可拉伸的圆角木纹底板（4 px 描边）。"""
    im = gen_wood(size, seed)
    d = ImageDraw.Draw(im)
    edge = rgb("#2a2018")
    d.rectangle([0, 0, size - 1, size - 1], outline=edge, width=3)
    d.rectangle([3, 3, size - 4, size - 4], outline=mix(rgb("#c9a271"), edge, 0.35), width=2)
    return im


def gen_button(size=96, seed=131):
    """按钮底：金属拉丝 + 描边 + 上高光下投影。"""
    im = gen_metal(size, seed)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, size - 1, size - 1], outline=rgb("#2a2018"), width=3)
    d.line([4, 4, size - 5, 4], fill=mix(rgb("#eef3f8"), rgb("#8d949c"), 0.3), width=2)
    d.line([4, size - 5, size - 5, size - 5], fill=rgb("#3b4046"), width=2)
    return im


GENERATORS = {
    "textures/paper.png": gen_paper,
    "textures/wood.png": gen_wood,
    "textures/metal.png": gen_metal,
    "textures/noise.png": gen_noise,
    "textures/spark.png": gen_spark,
    "textures/vignette.png": gen_vignette,
    "ui/panel.png": gen_panel,
    "ui/button.png": gen_button,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", action="append", default=[],
                    help="只生成指定相对路径（可重复），如 textures/spark.png")
    args = ap.parse_args()

    targets = args.only or list(GENERATORS)
    for rel in targets:
        if rel not in GENERATORS:
            raise SystemExit("unknown target: %s" % rel)
        dst = os.path.join(OUT, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        GENERATORS[rel]().save(dst, optimize=True)
        print("generated %s (%d bytes)" % (rel, os.path.getsize(dst)))


if __name__ == "__main__":
    main()
