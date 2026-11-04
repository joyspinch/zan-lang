"""Prepare original Wuwei artwork as WebP for the native Gui decoder (requires Pillow).

Landscape and prop art re-encodes from the extracted originals at q85
(method=6); small icons keep crisper edges at q90. title_calligraphy bakes the
original title shader (alpha = 1 - smoothstep(0.82, 0.94, min(rgb))) into a
lossless WebP so ink edges and the red seal stay pixel-exact. Only .webp
ships: stale .png files from earlier imports are removed from the output.
"""
from __future__ import annotations
import argparse
from pathlib import Path
from PIL import Image, ImageOps

TITLE = 'title_calligraphy'
ICON_MAX_SIDE = 256
ICON_QUALITY = 90
ART_QUALITY = 85


def apply_title_alpha(image: Image.Image) -> Image.Image:
    """Bake Main_decompiled.gd's title shader into alpha, leaving RGB/size intact."""
    image = image.convert('RGBA')
    # paper = min(c.rgb); alpha = 1 - smoothstep(0.82, 0.94, paper).
    factors = []
    for paper in range(256):
        t = max(0.0, min(1.0, (paper / 255.0 - 0.82) / (0.94 - 0.82)))
        factors.append(1.0 - t * t * (3.0 - 2.0 * t))
    # Multiply the original alpha, rounding only once to the nearest byte.
    rgba = image.tobytes()
    alpha = bytes(round(a * factors[min(r, g, b)])
                  for r, g, b, a in zip(rgba[0::4], rgba[1::4], rgba[2::4], rgba[3::4]))
    image.putalpha(Image.frombytes('L', image.size, alpha))
    return image


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path('D:/game/WuweiCultivation/assets_extracted/images'))
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'assets' / 'images')
    args = parser.parse_args()
    files = sorted(args.source.glob('*.webp'))
    if not files:
        parser.error(f'No original WebP artwork found in {args.source}')
    args.output.mkdir(parents=True, exist_ok=True)
    converted = 0
    total = 0
    for source in files:
        target = args.output / (source.stem + '.webp')
        if target.exists() and target.stat().st_mtime >= source.stat().st_mtime:
            total += target.stat().st_size
            continue
        with Image.open(source) as original:
            image = ImageOps.exif_transpose(original)
            if image.mode not in ('RGB', 'RGBA'):
                image = image.convert('RGBA' if 'A' in image.getbands() else 'RGB')
            if source.stem == TITLE:
                # Bake the original shader alpha once; lossless keeps ink edges
                # and the seal pixel-exact, exact keeps RGB of clear pixels.
                image = apply_title_alpha(image)
                image.save(target, format='WEBP', lossless=True, method=6, exact=True)
            else:
                # Preserve transparency and artwork, but cap costly oversized originals.
                image.thumbnail((1920, 1200), Image.Resampling.LANCZOS)
                quality = ICON_QUALITY if max(image.size) <= ICON_MAX_SIDE else ART_QUALITY
                image.save(target, format='WEBP', quality=quality, method=6)
        converted += 1
        total += target.stat().st_size
    # The pipeline ships WebP only: drop stale PNG copies from earlier imports.
    removed = 0
    for stale in args.output.glob('*.png'):
        stale.unlink()
        removed += 1
    print(f'Artwork ready: {len(files)} WebP images ({converted} converted) in {args.output}')
    print(f'Shipped size: {total / 1024:.0f} KiB; removed {removed} stale PNG files.')


if __name__ == '__main__':
    main()
