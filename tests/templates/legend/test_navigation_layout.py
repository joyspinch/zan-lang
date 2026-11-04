"""Validate actual UiDriver geometry, not a mock layout.
Usage: python test_navigation_layout.py <dump-tree.json> [more dumps...]
Coordinates are physical pixels; scale is derived from the rendered icon size.
"""
import json
from pathlib import Path
import sys

def walk(node):
    yield node
    for child in node.get('kids', []):
        yield from walk(child)

def check(path):
    root = json.loads(Path(path).read_text(encoding='utf-8-sig'))['root']
    nodes = [n for n in walk(root) if n.get('name','').startswith('nav-')]
    assert len(nodes) == 31, 'Missing or duplicate navigation controls'
    nav = {int(n['name'][4:]): n for n in nodes}
    assert set(nav) == set(range(31)), 'Missing or duplicate navigation route'
    # 原版实测：瓦片 32 逻辑高、图标 22、单行蓝字（底部两行瓦片 48）。
    # 图标是唯一与 DPI 无关的常量锚，用它推物理像素比例。
    scale = nav[0]['kids'][0]['w']/22
    for i, tile in nav.items():
        image, label = tile['kids']
        assert image['kind'] == 'Image' and label['kind'] == 'Label', (i,'child order')
        # 底部 30 号是两行文本瓦片（48 逻辑高），顶部/中部一律 32 逻辑高。
        expect_h = 48 if i == 30 else 32
        assert abs(tile['h']-expect_h*scale) <= 1, (i,'height')
        assert image['w'] == image['h'] == 22*scale, (i,'icon size')
        assert abs(image['x']-tile['x']-4*scale) <= 1, (i,'left inset')
        assert abs(label['x']-image['x']-image['w']-4*scale) <= 1, (i,'icon/text gap')
        assert label['h'] <= tile['h'] and label['h'] > 0, (i,'label height')
        assert abs(2*label['y']+label['h']-2*tile['y']-tile['h']) <= 2, (i,'label vertical center')
        assert abs(2*image['y']+image['h']-2*tile['y']-tile['h']) <= 1, (i,'vertical center')
        assert label['x']+label['w'] <= tile['x']+tile['w'], (i,'overflow')
    for start in (0,10,20):
        row = [nav[i] for i in range(start,start+10)]
        assert len({n['y'] for n in row}) == 1, 'Uneven row'
        assert max(n['w'] for n in row)-min(n['w'] for n in row) <= 1, 'Unequal columns'
        for a,b in zip(row,row[1:]):
            assert abs(b['x']-a['x']-a['w']-4*scale) <= 1, 'Uneven gap'
    for i in range(10):
        assert nav[i]['x'] == nav[i+10]['x'] and nav[i]['w'] == nav[i+10]['w'], 'Top rows misaligned'
        assert abs(nav[i+10]['y']-nav[i]['y']-nav[i]['h']-4*scale) <= 1, 'Uneven vertical gap'
    print(f'PASS {path}: 31 horizontal navigation buttons, scale={scale:g}')

if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit('Pass actual Zan UiDriver tree JSON files')
    for path in sys.argv[1:]:
        check(path)
