# Render brand SVGs via Edge headless over a keyed magenta backdrop,
# then PIL-downscale to all sizes. Transparent PNG out.
import re, shutil, subprocess, tempfile, time
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFilter

EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
SRC = Path("D:/project/zan-lang/assets/brand/logo")
DST = SRC / "png"
DST.mkdir(parents=True, exist_ok=True)
HOLE_MIN_PX = 200  # enclosed backdrop pockets (wordmark counters) -> transparent

# (svg, master(w,h), [(w,h), ...])
JOBS = [
    ("zan-mark.svg",           (2048, 2048), [(512, 512), (256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)]),
    ("zan-mark-mono.svg",      (1024, 1024), [(512, 512), (256, 256)]),
    ("zan-mark-white.svg",     (1024, 1024), [(512, 512)]),
    ("zan-wordmark.svg",       (1280, 640),  [(640, 320)]),
    ("zan-wordmark-white.svg", (1280, 640),  [(640, 320)]),
    ("zan-logo-horizontal.svg",      (1970, 1024), [(985, 512)]),
    ("zan-logo-horizontal-dark.svg", (1970, 1024), [(985, 512)]),
    ("zan-logo-vertical.svg",        (1280, 1592), [(640, 796)]),
]

def sized_svg(svg: Path, out: Path):
    # strip fixed width/height so the SVG fills the screenshot window,
    # and force a flat magenta backdrop (Edge paints an opaque page bg anyway)
    text = svg.read_text(encoding="utf-8")
    text = re.sub(r'\s(width|height)="[^"]*"', "", text, count=2)
    text = re.sub(r"(<svg[^>]*>)",
                  r'\1<rect width="100%" height="100%" fill="#FF00FF"/>',
                  text, count=1)
    out.write_text(text, encoding="utf-8")
    return out

def shoot(svg, out, w, h):
    if out.exists():
        out.unlink()  # stale screenshots must never pass the wait loop
    page_dir = Path(tempfile.mkdtemp(prefix="zanpage-"))
    try:
        page = sized_svg(SRC / svg, page_dir / "page.svg")
        for attempt in range(3):
            # Edge's launcher exits immediately on Windows; the page dir must
            # stay alive until the real process has written the screenshot.
            subprocess.run([
                EDGE, "--headless", "--disable-gpu", "--hide-scrollbars",
                "--no-first-run", "--no-default-browser-check",
                f"--user-data-dir={page_dir / 'profile'}",
                f"--screenshot={out}",
                f"--window-size={w},{h}",
                f"file:///{page.as_posix()}",
            ], capture_output=True, timeout=90)
            for _ in range(60):
                if out.exists() and out.stat().st_size > 1000:
                    time.sleep(0.5)
                    return True
                time.sleep(0.25)
            print(f"  retry {attempt + 1} for {out.name}", flush=True)
    finally:
        shutil.rmtree(page_dir, ignore_errors=True)
    return False

def key_magenta(im: Image.Image) -> Image.Image:
    im = im.convert("RGB")
    w, h = im.size
    r, g, b = im.split()
    magish = ImageChops.multiply(
        ImageChops.multiply(r.point(lambda p: 255 if p > 200 else 0),
                            b.point(lambda p: 255 if p > 200 else 0)),
        g.point(lambda p: 255 if p < 80 else 0))
    for seed in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1),
                 (w // 2, 0), (w // 2, h - 1), (0, h // 2), (w - 1, h // 2)]:
        if magish.getpixel(seed) == 255:
            ImageDraw.floodfill(magish, seed, 128, thresh=0)
    data = magish.load()
    for y in range(h):
        for x in range(w):
            if data[x, y] == 255:  # enclosed pockets: wordmark counters etc.
                ImageDraw.floodfill(magish, (x, y), 129, thresh=0)
                if magish.histogram()[129] >= HOLE_MIN_PX:
                    ImageDraw.floodfill(magish, (x, y), 128, thresh=0)
                else:
                    ImageDraw.floodfill(magish, (x, y), 255, thresh=0)
    alpha = magish.point(lambda p: 0 if p == 128 else 255)
    alpha = alpha.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(1.0))
    out = im.convert("RGBA")
    out.putalpha(alpha)
    return out

for svg, master, sizes in JOBS:
    stem = Path(svg).stem
    mw, mh = master
    master_png = DST / f"_{stem}-master.png"
    if not shoot(svg, master_png, mw, mh):
        print(f"{stem}: MASTER FAILED", flush=True)
        continue
    im = Image.open(master_png)
    if im.size != (mw, mh):
        im = im.resize((mw, mh), Image.LANCZOS)  # guard against Edge DPR drift
    im = key_magenta(im)
    bbox = im.getbbox()
    if not bbox:
        print(f"{stem}: EMPTY RENDER — ABORT", flush=True)
        master_png.unlink()
        continue
    for w, h in sizes:
        im.resize((w, h), Image.LANCZOS).save(DST / f"{stem}-{w}.png")
        print(f"{stem}-{w}.png: {(w, h)}", flush=True)
    master_png.unlink()
print("RENDER_DONE")
