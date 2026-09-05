"""Reproducibly slice the reviewed Image2 atlas; Pillow required (authoring only).
The model did not respect exact cell sizes. These reviewed cut lines are specific
 to navigation-atlas.png, not a general promise about generated sprite sheets.
"""
from pathlib import Path
import argparse
from PIL import Image

X = [0, 228, 436, 628, 816, 1000, 1158, 1320, 1536]
Y = [0, 296, 510, 766, 1024]
ROW_X = [X, X, [0,228,436,628,800,978,1158,1320,1536],
         [0,228,436,628,816,980,1158,1320,1536]]

def prepare(source, output):
    atlas = Image.open(source).convert('RGBA')
    if atlas.size != (1536, 1024):
        raise ValueError('Expected reviewed 1536x1024 navigation atlas')
    output.mkdir(parents=True, exist_ok=True)
    for i in range(32):
        col, row = i % 8, i // 8
        cuts = ROW_X[row]
        icon = atlas.crop((cuts[col], Y[row], cuts[col+1], Y[row+1]))
        # Flat near-black backdrop, soft 16-level matte preserves antialiased edges.
        icon.putdata([(r,g,b,max(0,min(255,(max(r,g,b)-24)*16)))
                      for r,g,b,a in icon.getdata()])
        bounds = icon.getbbox()
        if not bounds:
            raise ValueError(f'Empty sprite {i}')
        icon = icon.crop(bounds)
        icon.thumbnail((112,112), Image.Resampling.LANCZOS)
        tile = Image.new('RGBA', (128,128))
        tile.alpha_composite(icon, ((128-icon.width)//2,(128-icon.height)//2))
        tile.save(output/f'nav-{i}.png')

if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=root/'assets/generated/navigation-atlas.png')
    p.add_argument('--output', type=Path, default=root/'assets/generated/navigation')
    args = p.parse_args()
    prepare(args.source, args.output)
