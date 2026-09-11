#!/usr/bin/env python3
"""Chrome headless 布局 oracle（WEB_GUI_ROADMAP，P0 起建）。

"web 一样"由真实浏览器裁决：用例（JSON：css + body + selectors）包进
采集 harness，Chrome --headless --dump-dom 跑出每个选择器的
getBoundingClientRect，与 Zan 侧（css_test 的 Flow 段 / 未来 GenHtml 建树）
输出的 `sel x,y wxh` 行逐盒对比。

用法：
  python scripts/web_oracle.py tests/weboracle/basic.json            # 打印 Chrome 坐标
  python scripts/web_oracle.py tests/weboracle/basic.json --compare z.txt
      # 与 Zan 侧输出对比（行格式：`名字 x,y wxh`，名字按顺序对齐亦可）
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

CHROME_CANDIDATES = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    os.path.expandvars(r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe"),
    "/usr/bin/google-chrome",
    "/usr/bin/chromium-browser",
    "/usr/bin/chromium",
]

HARNESS = """<!doctype html><html><head><meta charset="utf-8">
<style>body {{ margin: 0; }}</style>
<style>{css}</style>
</head><body>
{body}
<script>
const sels = {sels};
const out = [];
for (const sel of sels) {{
  const el = document.querySelector(sel);
  if (!el) {{ out.push(sel + " missing"); continue; }}
  const r = el.getBoundingClientRect();
  out.push(sel + " " + r.x + "," + r.y + " " + r.width + "x" + r.height);
}}
document.title = "RECTS:" + out.join("|");
</script>
</body></html>"""


def find_chrome():
    for c in CHROME_CANDIDATES:
        if os.path.exists(c):
            return c
    return None


def chrome_rects(case):
    chrome = find_chrome()
    if not chrome:
        sys.exit("chrome not found; edit CHROME_CANDIDATES")
    html = HARNESS.format(css=case["css"], body=case["body"],
                          sels=json.dumps(case["selectors"]))
    fd, path = tempfile.mkstemp(suffix=".html")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(html)
    try:
        proc = subprocess.run(
            [chrome, "--headless=new", "--disable-gpu",
             "--virtual-time-budget=500", "--dump-dom", path],
            capture_output=True, text=True, timeout=60)
    finally:
        os.unlink(path)
    m = re.search(r"<title>(.*?)</title>", proc.stdout, re.S)
    if not m or "RECTS:" not in m.group(1):
        sys.exit("no RECTS in dump:\n" + proc.stdout[:2000])
    return m.group(1)[len("RECTS:"):].split("|")


def parse_side(path):
    """Zan 侧输出：`任意前缀 x,y wxh` —— 按行顺序与 selectors 对齐。"""
    rows = []
    for line in io_code(path):
        m = re.search(r"(-?\d+),(-?\d+)\s+(-?\d+)x(-?\d+)", line)
        if m:
            rows.append(tuple(int(g) for g in m.groups()))
    return rows


def io_code(path):
    with open(path, encoding="utf-8") as f:
        return f.read().splitlines()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case")
    ap.add_argument("--compare")
    ap.add_argument("--tol", type=int, default=1)
    args = ap.parse_args()
    with open(args.case, encoding="utf-8") as f:
        case = json.load(f)

    rects = chrome_rects(case)
    print("chrome rects:")
    for r in rects:
        print("  " + r)

    if args.compare:
        zan = parse_side(args.compare)
        sels = case["selectors"]
        bad = 0
        if len(zan) != len(sels):
            print(f"!! row count mismatch: zan {len(zan)} vs selectors {len(sels)}")
            bad += 1
        for i, sel in enumerate(sels):
            if i >= len(zan):
                break
            m = re.search(r"(-?\d+),(-?\d+)\s+(-?\d+)x(-?\d+)", rects[i])
            cx, cy, cw, chh = (int(g) for g in m.groups())
            zx, zy, zw, zh = zan[i]
            diff = max(abs(cx - zx), abs(cy - zy), abs(cw - zw), abs(chh - zh))
            tag = "ok" if diff <= args.tol else "DIFF"
            if diff > args.tol:
                bad += 1
            print(f"  [{tag}] {sel}: chrome {cx},{cy} {cw}x{chh}"
                  f"  zan {zx},{zy} {zw}x{zh}  (max {diff}px)")
        sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
