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
    scale = nav[0]['kids'][0]['w']/24
    for i, tile in nav.items():
        image, label = tile['kids']
        assert image['kind'] == 'Image' and label['kind'] == 'Label', (i,'child order')
        assert abs(tile['h']-40*scale) <= 1, (i,'height')
        assert image['w'] == image['h'] == 24*scale, (i,'icon size')
        assert abs(image['x']-tile['x']-4*scale) <= 1, (i,'left inset')
        assert abs(label['x']-image['x']-image['w']-4*scale) <= 1, (i,'icon/text gap')
        assert label['y'] == image['y'] and label['h'] == image['h'], (i,'baseline')
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
