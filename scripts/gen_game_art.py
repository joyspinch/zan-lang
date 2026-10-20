#!/usr/bin/env python3
"""gen_game_art.py — 用 OpenAI 兼容 images API 生成游戏素材并做后处理。

用法（一般由 scripts/gen_game_art.ps1 调用）：

    python scripts/gen_game_art.py --out assets/hero.png \
        --prompt "..." --size 1024x1024 --chroma 0,255,0 --trim --max 512

要点（来自 gpt-image-2 的实测行为）：
  - size 只认 1024x1024 / 1536x1024 / 1024x1536；
  - 返回永远是不透明 RGB，要透明 PNG 必须自己抠——绿幕色（chroma）
    抠图或几何蒙版；
  - 一次一张，30~90 秒；失败重试由调用方决定。
"""

import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request

from PIL import Image


def request_image(base_url, api_key, prompt, size, timeout):
    """调一次 images/generations，返回 (b64, revised_prompt)。"""
    url = base_url.rstrip("/") + "/images/generations"
    body = json.dumps({
        "model": "gpt-image-2",
        "prompt": prompt,
        "size": size,
        "n": 1,
    }).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=body,
        headers={
            "Authorization": "Bearer " + api_key,
            "Content-Type": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    item = data["data"][0]
    return item["b64_json"], item.get("revised_prompt", "")


def parse_chroma(text):
    if not text:
        return None
    parts = [int(p.strip()) for p in text.split(",")]
    if len(parts) != 3:
        raise SystemExit("--chroma 需要 'R,G,B' 三个数字")
    return tuple(parts)


def apply_chroma(im, chroma, tolerance):
    """把接近绿幕色的像素抠成透明，边缘按距离做软过渡。"""
    im = im.convert("RGBA")
    pixels = im.load()
    w, h = im.size
    cr, cg, cb = chroma
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            d = abs(r - cr) + abs(g - cg) + abs(b - cb)
            if d <= tolerance:
                pixels[x, y] = (r, g, b, 0)
            elif d <= tolerance * 2:
                # 软边：越接近绿幕越透明，避免硬锯齿
                alpha = int(a * (d - tolerance) / float(tolerance))
                pixels[x, y] = (r, g, b, max(0, min(a, alpha)))
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prompt", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", default="1024x1024")
    ap.add_argument("--base-url", default=os.environ.get("OPENAI_BASE_URL", ""))
    ap.add_argument("--api-key", default=os.environ.get("OPENAI_API_KEY", ""))
    ap.add_argument("--chroma", default="")
    ap.add_argument("--tolerance", type=int, default=70)
    ap.add_argument("--trim", action="store_true")
    ap.add_argument("--max", type=int, default=0, help="长边缩放到这个像素")
    ap.add_argument("--retries", type=int, default=3)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--raw", default="", help="同时保存未经后处理的原图")
    args = ap.parse_args()

    if not args.api_key or not args.base_url:
        raise SystemExit("缺少 OPENAI_API_KEY / OPENAI_BASE_URL（由 gen_game_art.ps1 提供）")

    b64 = ""
    revised = ""
    for attempt in range(1, args.retries + 1):
        try:
            b64, revised = request_image(
                args.base_url, args.api_key, args.prompt, args.size, args.timeout
            )
            break
        except Exception as exc:  # 网络/服务抖动：重试
            print("attempt %d failed: %s" % (attempt, exc), file=sys.stderr)
            if attempt == args.retries:
                raise
            time.sleep(3 * attempt)
    if not b64:
        raise SystemExit("没有拿到图片数据")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    raw_bytes = base64.b64decode(b64)
    tmp = args.out + ".raw.png"
    with open(tmp, "wb") as fh:
        fh.write(raw_bytes)

    im = Image.open(tmp).convert("RGBA")
    chroma = parse_chroma(args.chroma)
    if chroma is not None:
        im = apply_chroma(im, chroma, args.tolerance)
    if args.trim:
        bbox = im.getbbox()
        if bbox:
            im = im.crop(bbox)
    if args.max > 0:
        w, h = im.size
        scale = args.max / float(max(w, h))
        if scale < 1.0:
            im = im.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.LANCZOS)
    # 输出格式按扩展名走：.jpg 存 JPEG（体积小，适合背景这类不透明图），
    # 其余存 PNG（要保留 alpha 的精灵图必须走 PNG）。
    if args.out.lower().endswith((".jpg", ".jpeg")):
        im.convert("RGB").save(args.out, "JPEG", quality=85, optimize=True)
    else:
        im.save(args.out)

    if args.raw:
        with open(args.raw, "wb") as fh:
            fh.write(raw_bytes)
    else:
        try:
            os.remove(tmp)
        except OSError:
            pass

    print("GEN_OK %s %dx%d %d bytes" % (args.out, im.size[0], im.size[1],
                                        os.path.getsize(args.out)))
    if revised:
        print("REVISED_PROMPT: " + revised[:400])


if __name__ == "__main__":
    main()
