#!/usr/bin/env python3
"""check_game_templates.py — 模板层的门禁：素材不是色块 + 资源引用完整 + 模板互相独立。

三条检查（都对应 AGENTS.md 的硬约束）：

1. **资源引用完整性** —— 源码里写死的 `assets/xxx.png` 必须真的存在。
   防止"素材改名/删了，代码还在找"，那会在运行时静默变成色块。
2. **模板独立性** —— 模板之间不许互相引用，也不许存在"公共模板"让各项目套用。
   共享只允许发生在 packages/（机制）与 assets/game/（通用资源）。
3. **素材不是色块** —— 唯一颜色数 / 尺寸 / 体积下限。
   抠图精灵（带 alpha 通道）另有豁免：它们本来就只有几种颜色。

    python scripts/check_game_templates.py            # 全量
    python scripts/check_game_templates.py --json
    python scripts/check_game_templates.py --filter game/trap-cat
"""

import argparse
import json
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TPL_ROOT = os.path.join(ROOT, "templates")

# 源码里出现的资源路径（相对模板根）
ASSET_RE = re.compile(r"""["'](assets/[A-Za-z0-9_\-./]+\.(?:png|jpg|jpeg|bmp|ico|webp|gif|ogg|wav|mp3|html|css|json|ttf|otf))["']""")

# 模板之间互相引用的迹象：相对路径爬出模板根、或者出现兄弟模板目录名
CROSS_DIR_RE = re.compile(r"""["'](?:\.\./){2,}[A-Za-z0-9_\-/]*""")

# "公共模板"式的共享骨架目录名
PUBLIC_TEMPLATE_NAMES = {"common", "base", "shared", "core", "template", "_common", "_base", "_shared"}

MIN_COLORS = 64          # 非抠图图的最少唯一颜色数
MIN_DIM = 32            # 最小边长
MIN_BYTES = 4096        # 4 KiB
TEXTURE_SUFFIX = ("noise", "spark", "vignette", "paper", "wood", "metal")


def rel(path):
    return os.path.relpath(path, ROOT).replace("\\", "/")


def templates_under(root):
    out = []
    for dirpath, _dirs, files in os.walk(root):
        if "template.manifest" in files:
            out.append(dirpath)
    return sorted(out)


def check_assets(tpl_dir, findings):
    """源码引用的资源必须存在。"""
    src_ext = (".zan", ".html", ".css", ".json")
    for dirpath, _dirs, files in os.walk(tpl_dir):
        for name in files:
            if not name.endswith(src_ext):
                continue
            path = os.path.join(dirpath, name)
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            for m in ASSET_RE.finditer(text):
                asset = m.group(1)
                if not os.path.exists(os.path.join(tpl_dir, asset.replace("/", os.sep))):
                    findings.append({
                        "rule": "asset-missing",
                        "where": rel(path),
                        "detail": "引用了不存在的资源 %s" % asset,
                    })


def check_independence(tpl_dirs, findings):
    """模板之间不许互相引用，也不许有公共模板骨架。"""
    for tpl in tpl_dirs:
        name = os.path.basename(tpl)
        if name.lower() in PUBLIC_TEMPLATE_NAMES:
            findings.append({
                "rule": "public-template",
                "where": rel(tpl),
                "detail": "templates/ 下不允许公共模板目录 %r（共享走 packages/ 与 assets/game/）" % name,
            })
        siblings = {os.path.basename(d) for d in tpl_dirs} - {name}
        for dirpath, _dirs, files in os.walk(tpl):
            for fname in files:
                if not fname.endswith((".zan", ".html", ".css", ".json", ".md", ".proj")):
                    continue
                path = os.path.join(dirpath, fname)
                try:
                    with open(path, "r", encoding="utf-8", errors="replace") as f:
                        text = f.read()
                except OSError:
                    continue
                for m in CROSS_DIR_RE.finditer(text):
                    findings.append({
                        "rule": "cross-template-ref",
                        "where": rel(path),
                        "detail": "路径 %r 爬出了模板根，模板必须自包含" % m.group(1),
                    })
                for sib in siblings:
                    if sib in text and ("../%s/" % sib) in text.replace("\\", "/"):
                        findings.append({
                            "rule": "cross-template-ref",
                            "where": rel(path),
                            "detail": "引用了兄弟模板 %s" % sib,
                        })


def check_art(tpl_dir, findings):
    """素材不是色块。"""
    assets = os.path.join(tpl_dir, "assets")
    if not os.path.isdir(assets):
        return
    for dirpath, _dirs, files in os.walk(assets):
        for fname in files:
            if not fname.lower().endswith((".png", ".jpg", ".jpeg", ".bmp", ".webp")):
                continue
            path = os.path.join(dirpath, fname)
            size = os.path.getsize(path)
            if size < MIN_BYTES:
                findings.append({
                    "rule": "flat-art",
                    "where": rel(path),
                    "detail": "文件只有 %d 字节，低于 %d" % (size, MIN_BYTES),
                })
                continue
            try:
                with Image.open(path) as im:
                    im = im.convert("RGBA")
                    w, h = im.size
                    alpha = im.getchannel("A")
                    amin, amax = alpha.getextrema()
                    colors = im.convert("RGB").getcolors(maxcolors=1 << 20)
            except Exception as e:  # 读不出来的图本身就是问题
                findings.append({
                    "rule": "flat-art",
                    "where": rel(path),
                    "detail": "图片读不出来：%s" % e,
                })
                continue
            if w < MIN_DIM or h < MIN_DIM:
                findings.append({
                    "rule": "flat-art",
                    "where": rel(path),
                    "detail": "尺寸 %dx%d 过小（至少 %dx%d）" % (w, h, MIN_DIM, MIN_DIM),
                })
            name = fname.lower()
            is_cutout = amin != amax and "A" in im.getbands()  # 有透明通道 = 抠图精灵
            is_texture = any(name.startswith(t) or t in name for t in TEXTURE_SUFFIX)
            if is_texture:
                continue
            if is_cutout and colors is not None and len(colors) < MIN_COLORS:
                continue  # 抠图精灵颜色少是正常的
            if colors is not None and len(colors) < MIN_COLORS:
                findings.append({
                    "rule": "flat-art",
                    "where": rel(path),
                    "detail": "只有 %d 种颜色，像纯色块（至少 %d）" % (len(colors), MIN_COLORS),
                })


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--filter", default="", help="只查某个模板目录名，如 game/trap-cat")
    args = ap.parse_args()

    tpls = templates_under(TPL_ROOT)
    if args.filter:
        tpls = [t for t in tpls if rel(t).replace("\\", "/").endswith(args.filter)]

    findings = []
    for tpl in tpls:
        check_assets(tpl, findings)
        check_art(tpl, findings)
    check_independence(tpls, findings)

    if args.json:
        print(json.dumps({"templates": [rel(t) for t in tpls], "findings": findings},
                         ensure_ascii=False, indent=2))
    else:
        for tpl in tpls:
            print("checked %s" % rel(tpl))
        for f in findings:
            print("FAIL [%s] %s: %s" % (f["rule"], f["where"], f["detail"]))
        print("game-template check: %d template(s), %d finding(s)" % (len(tpls), len(findings)))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
