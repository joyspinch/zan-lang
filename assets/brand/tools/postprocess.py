# Post-process the AI-generated Z-Goose & Zan-chan VI masters into shipped assets.
# Inputs: 1024x1024 opaque RGB PNGs on a white / green background, from gpt-image-2.
# Outputs: mascot/*.png (transparent), avatar(-round), assets/zan.ico, assets/zan_ios_icon.png.
import sys
from pathlib import Path
from PIL import Image, ImageFilter, ImageDraw

BRAND = Path(__file__).resolve().parent.parent          # assets/brand
REPO = BRAND.parent.parent                              # repo root
RAW = Path(sys.argv[1]) if len(sys.argv) > 1 else BRAND / "_raw"

def key_bg(im: Image.Image) -> Image.Image:
    import numpy as np
    im = im.convert("RGB")
    arr = np.array(im)
    border = np.concatenate([arr[:8].reshape(-1, 3), arr[-8:].reshape(-1, 3),
                             arr[:, :8].reshape(-1, 3), arr[:, -8:].reshape(-1, 3)])
    med = np.median(border, axis=0)
    r, g, b = med
    if g > 140 and r < 170 and b < 170:
        rr, gg, bb = im.split()
        greenish = Image.eval(gg, lambda p: 255 if p > 150 else 0)
        r_ok = Image.eval(rr, lambda p: 255 if p < 170 else 0)
        b_ok = Image.eval(bb, lambda p: 255 if p < 170 else 0)
        from PIL import ImageChops
        m = ImageChops.multiply(ImageChops.multiply(greenish, r_ok), b_ok)
    elif r > 240 and g > 240 and b > 240:
        # white background sticker
        gray = im.convert("L")
        bg_cand = gray.point(lambda p: 255 if p > 248 else 0)
        pad = Image.new("L", (im.width + 2, im.height + 2), 255)
        pad.paste(bg_cand, (1, 1))
        ImageDraw.floodfill(pad, (0, 0), 128, thresh=0)
        m = pad.crop((1, 1, im.width + 1, im.height + 1)).point(
            lambda p: 255 if p == 128 else 0)
        alpha = m.point(lambda p: 0 if p == 255 else 255)
        alpha = alpha.filter(ImageFilter.GaussianBlur(0.8))
        out = im.convert("RGBA")
        out.putalpha(alpha)
        return out
    else:
        lum = (arr[..., 0] * 0.3 + arr[..., 1] * 0.6 + arr[..., 2] * 0.1)
        dark = (lum < 160).astype(np.uint8) * 255
        m = Image.fromarray(dark, "L")
        pad = Image.new("L", (m.width + 2, m.height + 2), 255)
        pad.paste(m, (1, 1))
        ImageDraw.floodfill(pad, (0, 0), 128, thresh=0)
        m = pad.crop((1, 1, m.width + 1, m.height + 1)).point(
            lambda p: 255 if p == 128 else 0)
        alpha = m.point(lambda p: 0 if p == 255 else 255)
        alpha = alpha.filter(ImageFilter.MinFilter(5)).filter(ImageFilter.MinFilter(5))
        out = im.convert("RGBA")
        out.putalpha(alpha)
        return out
    alpha = m.point(lambda p: 0 if p == 255 else 255)
    alpha = alpha.filter(ImageFilter.MinFilter(3)).filter(
        ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.8))
    out = im.convert("RGBA")
    out.putalpha(alpha)
    return out

def crop_margin(im: Image.Image, pad: int = 16) -> Image.Image:
    bbox = im.getchannel("A").getbbox()
    if bbox:
        x0, y0, x1, y1 = bbox
        return im.crop((max(0, x0 - pad), max(0, y0 - pad),
                        min(im.width, x1 + pad), min(im.height, y1 + pad)))
    return im

# ---- mascots: transparent PNG, cropped to silhouette ----
for name in ["hero", "cheer", "battle"]:
    src = RAW / f"{name}.png"
    if not src.exists():
        continue
    im = crop_margin(key_bg(Image.open(src)), 24)
    im.save(BRAND / f"mascot/zanzi-{name}.png")
    print(f"zanzi-{name}.png ok", flush=True)

# ---- avatar from mark ----
mark_512 = BRAND / "logo/png/zan-mark-512.png"
if mark_512.exists():
    im_mark = Image.open(mark_512)
    im_mark.save(BRAND / "mascot/zanzi-avatar.png")
    round_im = im_mark.copy()
    m = Image.new("L", (512, 512), 0)
    ImageDraw.Draw(m).ellipse([16, 16, 496, 496], fill=255)
    m = m.filter(ImageFilter.GaussianBlur(1.2))
    round_im.putalpha(m)
    round_im.save(BRAND / "mascot/zanzi-avatar-round.png")
    print("avatar ok", flush=True)

print("POSTPROCESS_DONE", flush=True)
