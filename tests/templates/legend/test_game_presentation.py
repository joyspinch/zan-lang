"""Check real UiDriver dumps, never synthetic layout snapshots.

python tests/templates/legend/test_game_presentation.py initial-tree.json \
    --scrolled scrolled-tree.json --pixels initial.pixels
"""
import argparse
import json
from pathlib import Path
import struct
from PIL import Image


def walk(node):
    yield node
    for child in node.get('kids', []):
        yield from walk(child)


def read(path):
    root = json.loads(Path(path).read_text(encoding='utf-8-sig'))['root']
    return {n['name']: n for n in walk(root) if n.get('name')}


def check(path, scrolled=None, pixels=None):
    nodes = read(path)
    scale = nodes['nav-0']['kids'][0]['w'] / 28
    for prefix in ('income', 'chat'):
        viewport, text = nodes[prefix+'-scroll'], nodes[prefix+'-content']
        assert viewport['kind'] == 'ScrollColumn', prefix
        assert text['y'] == viewport['y'], (prefix, 'content is not top aligned')
        assert text['h'] == text['prefH'], (prefix, 'content stretched instead of natural height')
        assert text['w'] < viewport['w'], (prefix, 'no scrollbar reserve')
    assert nodes['chat-content']['h'] < nodes['chat-scroll']['h'], 'Fixture must include short chat content'
    for prefix in ('enemy', 'player'):
        text, bar, unit = (nodes[prefix+k] for k in ('-health-text', '-health-bar', '-unit'))
        assert abs(bar['h'] - 8*scale) <= 1, (prefix, 'track would exceed allocation')
        assert bar['y'] >= text['y'] + text['h'] + 4*scale, (prefix, 'HP text overlaps track')
        assert bar['x'] == text['x'] and bar['w'] == text['w'], (prefix, 'HP alignment')
        assert bar['y'] + bar['h'] <= unit['y'] + unit['h'], (prefix, 'HP outside unit')
    name, text, bar, sprite = (nodes[n] for n in ('enemy-name', 'enemy-health-text', 'enemy-health-bar', 'enemy-sprite'))
    assert name['y'] + name['h'] <= text['y'], 'Enemy name overlap'
    assert sprite['y'] >= bar['y'] + bar['h'] + 4*scale, 'Sprite/track overlap'
    assert sprite['y'] + sprite['h'] <= nodes['pause']['y'], 'Sprite overlaps controls'
    if scrolled:
        assert nodes['income-content']['h'] > nodes['income-scroll']['h'], 'Fixture must overflow the income viewport'
        later = read(scrolled)
        assert later['income-content']['y'] < later['income-scroll']['y'], 'Income scroll did not move'
        assert later['chat-content']['y'] == nodes['chat-content']['y'], 'Income wheel moved chat'
    if pixels:
        data = Path(pixels).read_bytes()
        magic, x, y, w, h, bpp = struct.unpack('<6I', data[:24])
        assert magic == 0x3158505a and bpp == 4
        image = Image.frombytes('RGBA', (w,h), data[24:], 'raw', 'BGRA')
        for prefix in ('enemy', 'player'):
            bar = nodes[prefix+'-health-bar']
            row = [image.getpixel((px-x, bar['y'] + bar['h']//2-y))[:3]
                   for px in range(bar['x']+3, bar['x']+bar['w']-3)]
            assert (201,75,72) in row, (prefix, 'HP fill skin not applied')
            assert set(row) <= {(201,75,72), (16,24,33)}, (prefix, 'unexpected track background')
    print(f'PASS {path}: natural-height top-aligned logs/chat; separate names, HP tracks and sprites; scale={scale:g}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tree')
    parser.add_argument('--scrolled')
    parser.add_argument('--pixels')
    args = parser.parse_args()
    check(args.tree, args.scrolled, args.pixels)
