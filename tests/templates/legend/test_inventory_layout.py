"""Validate the eight-slot cultivation UI from real UiDriver before/scrolled dumps."""
import argparse
import sys
sys.dont_write_bytecode = True
from test_game_presentation import read


def check(before, scrolled):
    initial, later = read(before), read(scrolled)
    slots = [initial[f'inner-up-{i}'] for i in range(8)]
    scale = initial['nav-0']['kids'][0]['w'] / 28
    assert len({s['h'] for s in slots}) == 1
    assert abs(slots[0]['h'] - 28*scale) <= 1
    for i in range(0, 8, 2):
        left, right = slots[i:i+2]
        assert left['y'] == right['y'], 'Cultivation row misaligned'
        assert abs(left['w']-right['w']) <= 1
        assert right['x'] >= left['x'] + left['w'] + 4*scale
        if i:
            assert left['x'] == slots[0]['x']
            assert left['y'] > slots[i-2]['y'] + slots[i-2]['h']
    viewport = later['equipment-details-scroll']
    for i in (6, 7):
        button = later[f'inner-up-{i}']
        assert button['y'] >= viewport['y'], 'Last cultivation row is unreachable'
        assert button['y'] + button['h'] <= viewport['y'] + viewport['h']
    assert later['inner-up-0']['y'] < initial['inner-up-0']['y'], 'Details did not scroll'
    assert initial['nav-20']['y'] == later['nav-20']['y'], 'Details scrolled the outer page'
    print('PASS eight cultivation slots: equal targets, two columns, last row reachable by scrolling')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('before'); p.add_argument('scrolled')
    a = p.parse_args(); check(a.before, a.scrolled)
