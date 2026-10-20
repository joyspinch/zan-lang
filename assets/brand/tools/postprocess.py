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

def key_bg(im: Image.Image) -> Image.Image:
    # Adaptive background removal.  The API mostly honors the chroma-green
    # instruction, but some gens come back on a pure black card instead —
    # detect the backdrop from the border median and pick the key.
    import numpy as np
    from PIL import ImageDraw
    im = im.convert("RGB")
    arr = np.array(im)
    border = np.concatenate([arr[:8].reshape(-1, 3), arr[-8:].reshape(-1, 3),
                             arr[:, :8].reshape(-1, 3), arr[:, -8:].reshape(-1, 3)])
    med = np.median(border, axis=0)
    r, g, b = med
    if g > 140 and r < 170 and b < 170:
        # chroma green: the sticker style guarantees the character itself
        # has no green (cream fur / brown outline / blue scarf), so every
        # greenish pixel is background
        rr, gg, bb = im.split()
        greenish = Image.eval(gg, lambda p: 255 if p > 150 else 0)
        r_ok = Image.eval(rr, lambda p: 255 if p < 170 else 0)
        b_ok = Image.eval(bb, lambda p: 255 if p < 170 else 0)
        m = ImageChops_mul(greenish, r_ok, b_ok)
    elif r < 70 and g < 70 and b < 70:
        # black card: flood-fill the dark region reachable from the border
        # (the white die-cut border keeps the fill out of the character)
        dark = ((arr[..., 0] < 80) & (arr[..., 1] < 80)
                & (arr[..., 2] < 80)).astype(np.uint8) * 255
        m = Image.fromarray(dark, "L")
        pad = Image.new("L", (m.width + 2, m.height + 2), 255)
        pad.paste(m, (1, 1))
        ImageDraw.floodfill(pad, (0, 0), 128, thresh=0)
        m = pad.crop((1, 1, m.width + 1, m.height + 1)).point(
            lambda p: 255 if p == 128 else 0)
    else:
        print(f"  unknown backdrop median rgb=({r:.0f},{g:.0f},{b:.0f}) — SKIP",
              flush=True)
        return im.convert("RGBA")
    alpha = m.point(lambda p: 0 if p == 255 else 255)
    alpha = alpha.filter(ImageFilter.MinFilter(3)).filter(
        ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.8))
    out = im.convert("RGBA")
    out.putalpha(alpha)
    return drop_specks(out)

def ImageChops_mul(a, b, c):
    from PIL import ImageChops
    return ImageChops.multiply(ImageChops.multiply(a, b), c)

def drop_specks(im: Image.Image, min_area: int = 600) -> Image.Image:
    # AI gens leave small white blotches on the green screen (glow bleed)
    # that the chroma key can't see.  Remove connected solid-alpha islands
    # smaller than min_area — the drone buddy and speed streaks are far
    # bigger and survive; speckles die together with their feathered halo.
    import numpy as np
    from collections import deque
    a = np.array(im.getchannel("A"))
    solid = a > 128
    H, W = solid.shape
    lbl = np.zeros((H, W), np.int32)
    drop = np.zeros((H, W), bool)
    nxt = 0
    for sy in range(H):
        for sx in np.nonzero(solid[sy] & (lbl[sy] == 0))[0]:
            sx = int(sx)
            if lbl[sy, sx]:
                continue
            nxt += 1
            q = deque([(sy, sx)])
            lbl[sy, sx] = nxt
            area = 0
            while q:
                y, x = q.popleft()
                area += 1
                if y > 0 and solid[y - 1, x] and not lbl[y - 1, x]:
                    lbl[y - 1, x] = nxt; q.append((y - 1, x))
                if y < H - 1 and solid[y + 1, x] and not lbl[y + 1, x]:
                    lbl[y + 1, x] = nxt; q.append((y + 1, x))
                if x > 0 and solid[y, x - 1] and not lbl[y, x - 1]:
                    lbl[y, x - 1] = nxt; q.append((y, x - 1))
                if x < W - 1 and solid[y, x + 1] and not lbl[y, x + 1]:
                    lbl[y, x + 1] = nxt; q.append((y, x + 1))
            if area < min_area:
                drop |= lbl == nxt
    if drop.any():
        m = Image.fromarray((drop * 255).astype(np.uint8)).filter(
            ImageFilter.MaxFilter(7))
        a[np.array(m) > 0] = 0
        im.putalpha(Image.fromarray(a))
    return im

def crop_margin(im: Image.Image, pad: int) -> Image.Image:
    bbox = im.getchannel("A").getbbox()
    x0, y0, x1, y1 = bbox
    return im.crop((max(0, x0 - pad), max(0, y0 - pad),
                    min(im.width, x1 + pad), min(im.height, y1 + pad)))

# ---- mascots: transparent PNG, cropped to silhouette ----
for name in ["hero", "run", "sleep", "cheer", "code", "battle"]:
    src = RAW / f"{name}.png"
    im = key_bg(Image.open(src))
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
# Only when the icon master is present; mascot-only re-runs keep the
# committed zan.ico / zan_ios_icon.png untouched.
if (RAW / "icon.png").exists():
    icon = crop_margin(key_bg(Image.open(RAW / "icon.png")), 0)
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
else:
    print("no icon.png in raw dir — keeping committed zan.ico / zan_ios_icon.png",
          flush=True)

# ---- self check ----
ico = Image.open(REPO / "assets/zan.ico")
print("ico:", ico.size, ico.mode, sorted(ico.info.get("sizes", [])), flush=True)
for p in [BRAND / "mascot/zanzi-hero.png", REPO / "assets/zan_ios_icon.png"]:
    im = Image.open(p)
    print(p.name, im.size, im.mode, flush=True)
print("POSTPROCESS_DONE", flush=True)
