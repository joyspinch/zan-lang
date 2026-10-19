# Post-process the AI-generated rabbit VI masters into shipped assets.
# Inputs: 1024x1024 opaque RGB PNGs on a solid chroma-key green (#00FF00)
# background (icon + hero/run/sleep/cheer/code), from gpt-image-2.
# Outputs: mascot/*.png (transparent), avatar(-round), assets/zan.ico,
# assets/zan_ios_icon.png.  Idempotent: safe to re-run.
import sys
from pathlib import Path
from PIL import Image, ImageFilter, ImageDraw

BRAND = Path(__file__).resolve().parent.parent          # assets/brand
REPO = BRAND.parent.parent                              # repo root
RAW = Path(sys.argv[1]) if len(sys.argv) > 1 else BRAND / "_raw"

def key_green(im: Image.Image) -> Image.Image:
    # Global chroma key: the flat sticker style guarantees the character
    # itself contains no green (cream fur / brown outline / blue scarf), so
    # every greenish pixel is background.  Erode 1px to eat the blend fringe,
    # then soften for clean edges.
    im = im.convert("RGB")
    r, g, b = im.split()
    greenish = Image.eval(g, lambda p: 255 if p > 150 else 0)
    r_ok = Image.eval(r, lambda p: 255 if p < 170 else 0)
    b_ok = Image.eval(b, lambda p: 255 if p < 170 else 0)
    mask = ImageChops_mul(greenish, r_ok, b_ok)          # 255 = background
    alpha = mask.point(lambda p: 0 if p == 255 else 255)
    alpha = alpha.filter(ImageFilter.MinFilter(3)).filter(
        ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.8))
    out = im.convert("RGBA")
    out.putalpha(alpha)
    return out

def ImageChops_mul(a, b, c):
    from PIL import ImageChops
    return ImageChops.multiply(ImageChops.multiply(a, b), c)

def crop_margin(im: Image.Image, pad: int) -> Image.Image:
    bbox = im.getchannel("A").getbbox()
    x0, y0, x1, y1 = bbox
    return im.crop((max(0, x0 - pad), max(0, y0 - pad),
                    min(im.width, x1 + pad), min(im.height, y1 + pad)))

# ---- mascots: transparent PNG, cropped to silhouette ----
for name in ["hero", "run", "sleep", "cheer", "code"]:
    src = RAW / f"{name}.png"
    im = key_green(Image.open(src))
    n = sum(1 for p in im.getchannel("A").getdata() if p == 0)
    if n < im.width * im.height * 0.2:
        print(f"{name}: SUSPICIOUS — only {n} bg px, keying likely failed", flush=True)
    crop_margin(im, 24).save(BRAND / f"mascot/zanzi-{name}.png")
    print(f"zanzi-{name}.png ok", flush=True)

# ---- avatar: square head crop + round variant ----
# Chibi hero: ear-to-ear IS the head width, so a full-bbox-width square
# anchored at the silhouette top frames ears + face with eyes in the
# upper third — the classic head-avatar crop.
hero = Image.open(BRAND / "mascot/zanzi-hero.png")
x0, y0, x1, y1 = hero.getchannel("A").getbbox()
side = x1 - x0
top = max(0, y0 - 10)
sx = max(0, min(hero.width - side, x0))
if top + side > hero.height:
    top = hero.height - side
head = hero.crop((sx, top, sx + side, top + side))
head = head.resize((512, 512), Image.LANCZOS)
head.save(BRAND / "mascot/zanzi-avatar.png")
round_im = head.copy()
m = Image.new("L", (1024, 1024), 0)
ImageDraw.Draw(m).ellipse([8, 8, 1016, 1016], fill=255)
m = m.resize((512, 512), Image.LANCZOS).filter(ImageFilter.GaussianBlur(1.2))
round_im.putalpha(m)
round_im.save(BRAND / "mascot/zanzi-avatar-round.png")
print("avatar ok", flush=True)

# ---- app icon: zan.ico (multi-size) + iOS 256 RGB ----
icon = crop_margin(key_green(Image.open(RAW / "icon.png")), 0)
side = min(icon.size)
icon = icon.crop(((icon.width - side) // 2, (icon.height - side) // 2,
                  (icon.width + side) // 2, (icon.height + side) // 2))
icon = icon.resize((1024, 1024), Image.LANCZOS)
sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]
icon.resize((256, 256), Image.LANCZOS).save(
    REPO / "assets/zan.ico", sizes=sizes)
white = Image.new("RGB", icon.size, (255, 255, 255))
white.paste(icon, (0, 0), icon)
white.resize((256, 256), Image.LANCZOS).save(REPO / "assets/zan_ios_icon.png")
print("zan.ico / zan_ios_icon.png ok", flush=True)

# ---- self check ----
ico = Image.open(REPO / "assets/zan.ico")
print("ico:", ico.size, ico.mode, sorted(ico.info.get("sizes", [])), flush=True)
for p in [BRAND / "mascot/zanzi-hero.png", REPO / "assets/zan_ios_icon.png"]:
    im = Image.open(p)
    print(p.name, im.size, im.mode, flush=True)
print("POSTPROCESS_DONE", flush=True)
