# Compose GitHub banner + palette card for the Zan VI.
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

BRAND = Path("D:/project/zan-lang/assets/brand")
OUT = BRAND / "banner"
OUT.mkdir(parents=True, exist_ok=True)

VOLT, VOLT_DEEP, SKY = (43, 92, 255), (33, 65, 230), (125, 162, 255)
SPARK, INK, PAPER = (255, 200, 56), (15, 20, 40), (250, 251, 255)
GRAY = (154, 165, 192)

F = "C:/Windows/Fonts/"
def font(name, size):
    return ImageFont.truetype(F + name, size)

def vgrad(w, h, top, bottom):
    base = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        base.putpixel((0, y), tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    return base.resize((w, h))

# ---------------- banner (1500 x 500) ----------------
W, H = 1500, 500
banner = vgrad(W, H, (16, 21, 44), (10, 13, 30)).convert("RGB")

# radial glows (computed small, upscaled)
glow = Image.new("L", (150, 50), 0)
gp = glow.load()
for y in range(50):
    for x in range(150):
        d1 = ((x - 18) / 60) ** 2 + ((y - 8) / 22) ** 2
        d2 = ((x - 135) / 70) ** 2 + ((y - 46) / 26) ** 2
        v = int(max(0, 90 * (1 - d1)) + max(0, 110 * (1 - d2)))
        gp[x, y] = min(255, v)
glow = glow.resize((W, H), Image.BICUBIC).filter(ImageFilter.GaussianBlur(18))
banner = Image.composite(vgrad(W, H, VOLT, VOLT_DEEP), banner, glow.point(lambda p: p // 2))

# dot grid
dots = Image.new("L", (W, H), 0)
dd = ImageDraw.Draw(dots)
for gy in range(24, H, 46):
    for gx in range(24, W, 46):
        dd.ellipse([gx - 1, gy - 1, gx + 1, gy + 1], fill=34)
tint = Image.new("RGB", (W, H), SKY)
banner = Image.composite(tint, banner, dots)

d = ImageDraw.Draw(banner)
# left: pre-composed horizontal lockup (tuned proportions) + tagline
lock = Image.open(BRAND / "logo/png/zan-logo-horizontal-dark-985.png").resize((700, 364), Image.LANCZOS)
banner.paste(lock, (64, 44), lock)
f_tag = font("consola.ttf", 44)
d.text((88, 424), "AOT. ARC. Adorable.", font=f_tag, fill=(143, 163, 216))

# right: mascot hero, bottom-aligned
hero = Image.open(BRAND / "mascot/zanzi-hero.png")
hh = 540
hero = hero.resize((int(hero.width * hh / hero.height), hh), Image.LANCZOS)
banner.paste(hero, (W - hero.width - 36, H - hh + 30), hero)

# thin volt baseline
d.rectangle([0, H - 6, W, H], fill=VOLT)
banner.save(OUT / "zan-banner.png")
print("banner ok", banner.size)

# ---------------- palette card (1600 x 1180) ----------------
PW, PH = 1600, 1180
card = Image.new("RGB", (PW, PH), PAPER)
cd = ImageDraw.Draw(card)
cd.text((90, 78), "Zan Visual Identity", font=font("segoeuib.ttf", 64), fill=INK)
cd.text((92, 168), "Color System · 主色板", font=font("msyh.ttc", 30), fill=GRAY)
mini = Image.open(BRAND / "logo/png/zan-logo-vertical-640.png")
mini = mini.resize((110, 137), Image.LANCZOS)
card.paste(mini, (PW - 200, 70), mini)

swatches = [
    ("Zan Volt",     "#2B5CFF", "Primary · 主色",  VOLT),
    ("Volt Deep",    "#2141E6", "Gradient / Hover", VOLT_DEEP),
    ("Zan Sky",      "#7DA2FF", "Tint · 浅底",     SKY),
    ("Zan Spark",    "#FFC838", "Accent · 点缀",   SPARK),
    ("Zan Cream",    "#FFF3DC", "Surface · 暖底",  (255, 243, 220)),
    ("Zan Ink",      "#0F1428", "Text / Dark bg",  INK),
]
sx, sy, sw, sh, gap = 90, 260, 440, 240, 35
for i, (name, hexc, role, rgb) in enumerate(swatches):
    x = sx + (i % 3) * (sw + gap)
    y = sy + (i // 3) * (sh + gap)
    cd.rounded_rectangle([x, y, x + sw, y + sh], radius=28, fill=rgb,
                         outline=(224, 230, 245), width=2)
    dark_bg = sum(rgb) < 420
    fg = (255, 255, 255) if dark_bg else INK
    cd.text((x + 28, y + 26), name, font=font("segoeuib.ttf", 34), fill=fg)
    cd.text((x + 28, y + 78), role, font=font("msyh.ttc", 22),
            fill=fg if not dark_bg else (210, 220, 245))
    cd.text((x + 28, y + sh - 62), hexc.upper(), font=font("consola.ttf", 34), fill=fg)

sy2 = sy + 2 * (sh + gap) + 44
cd.text((90, sy2), "Semantic · 语义色", font=font("msyh.ttc", 28), fill=INK)
sem = [("OK", "#2FBF71", (47, 191, 113)), ("Warning", "#FF8A3D", (255, 138, 61)),
       ("Error", "#FF5C5C", (255, 92, 92)), ("Info", "#2B5CFF", VOLT)]
for i, (name, hexc, rgb) in enumerate(sem):
    x = sx + i * (sw // 2 + gap)
    y = sy2 + 52
    cd.rounded_rectangle([x, y, x + sw // 2, y + 96], radius=20, fill=rgb)
    cd.text((x + 22, y + 16), name, font=font("segoeuib.ttf", 26), fill=(255, 255, 255))
    cd.text((x + 22, y + 54), hexc.upper(), font=font("consola.ttf", 24), fill=(255, 255, 255))

# mascot strip at the bottom, below the semantic row
strip_names = ["zanzi-run", "zanzi-sleep", "zanzi-cheer", "zanzi-code"]
strip_x = PW - 90
strip_y = sy2 + 52 + 96 + 36
for n in reversed(strip_names):
    im = Image.open(BRAND / f"mascot/{n}.png")
    th = 200
    im = im.resize((int(im.width * th / im.height), th), Image.LANCZOS)
    strip_x -= im.width + 18
    card.paste(im, (strip_x, strip_y), im)

card.save(OUT / "zan-palette.png")
print("palette ok", card.size)
