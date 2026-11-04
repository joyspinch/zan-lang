"""Real Windows UiDriver routing regression; launches only isolated save copies.
Usage: python tests/templates/legend/test_map_routes.py build/legend-map-routing/Legend.exe
Outputs stay under _scratch/legend/map-routing-regression, never in the user save.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / '_scratch/legend/map-routing-regression'

def walk(node):
    yield node
    for child in node.get('kids', []):
        yield from walk(child)

def tree(name):
    return list(walk(json.loads((OUT / f'{name}.json').read_text(encoding='utf-8-sig'))['root']))

def node(nodes, name):
    matches = [n for n in nodes if n.get('name') == name]
    assert len(matches) == 1, (name, len(matches))
    return matches[0]

def click(nodes, name):
    n = node(nodes, name)
    return f"click {n['x'] + n['w']//2} {n['y'] + n['h']//2}\nwait 250"

def selector(nodes, ids):
    grid = node(nodes, 'map-selector')
    assert not any(n.get('name') == 'challenge' for n in nodes), 'Map selector became illusion combat'
    actual = {int(n['name'][4:]) for n in nodes if n.get('name', '').startswith('map-') and n['name'][4:].isdigit()}
    assert actual == set(ids), actual
    assert len(grid['kids']) == len(actual)
    viewport = node(nodes, 'map-scroll')
    previous = node(nodes, 'map-prev')
    assert viewport['h'] > 0 and viewport['y'] + viewport['h'] <= previous['y'], 'Pager must remain outside scrolling maps'
    for start in range(0, len(grid['kids']), 4):
        row = grid['kids'][start:start+4]
        assert len({n['y'] for n in row}) == 1
    names = [n['name'] for n in nodes if n.get('kind') == 'Label']
    assert '地图传送' in names

def fixture():
    return dict(version=3, savedAt=int(time.time()), job=0, level=10, floor=6,
                best=10, hp=200, mp=60, active=0, autoPush=0, gear=[{} for _ in range(12)],
                bag=[], storage=[], codex=[0]*12)

def run(exe, name, commands, fresh=True):
    OUT.mkdir(parents=True, exist_ok=True)
    save = OUT / 'save'; save.mkdir(exist_ok=True)
    if fresh:
        (save / 'hero-v2.json').write_text(json.dumps(fixture()), encoding='utf-8')
    script = OUT / f'{name}.ui'
    script.write_text('wait 700\n' + '\n'.join(commands) + '\nquit\n', encoding='utf-8')
    env = dict(os.environ, LEGEND_SAVE_DIR=str(save), ZAN_UI_SCRIPT=str(script), ZAN_UI_OUT=str(OUT),
               LEGEND_OFFLINE='1')
    startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
    subprocess.run([str(exe)], cwd=exe.parent, env=env, startupinfo=startup, timeout=20, check=True)
    log = (OUT / 'results.log').read_text(encoding='utf-8-sig')
    assert 'DONE' in log and 'fail=0' in log, log

def main():
    exe = Path(sys.argv[1]).resolve()
    run(exe, 'startup', ['dump tree startup.json'])
    startup = tree('startup')
    run(exe, 'maps', [click(startup, 'nav-0'), 'dump tree maps.json', 'dump pixels maps.pixels'])
    maps = tree('maps'); selector(maps, range(1,21))
    viewport = node(maps, 'map-scroll')
    scroll = f"move {viewport['x'] + viewport['w']//2} {viewport['y'] + viewport['h']//2}\nwait 150\nscroll {viewport['x'] + viewport['w']//2} {viewport['y'] + viewport['h']//2} -2400\nwait 250"
    run(exe, 'scroll', [click(startup, 'nav-0'), scroll, 'dump tree scrolled.json'])
    scrolled = tree('scrolled'); selector(scrolled, range(1,21))
    last = node(scrolled, 'map-20'); view = node(scrolled, 'map-scroll')
    assert view['y'] <= last['y'] and last['y'] + last['h'] <= view['y'] + view['h'], 'Last map row must be reachable'
    assert node(scrolled, 'map-next')['y'] == node(maps, 'map-next')['y'], 'Scrolling moved pager'
    run(exe, 'locked', [click(startup, 'nav-0'), click(maps, 'map-3'), 'dump tree locked.json'])
    selector(tree('locked'), range(1,21))
    state = json.loads((OUT/'save/hero-v2.json').read_text(encoding='utf-8-sig'))
    assert state['floor'] == 6 and state['best'] == 10, 'Locked destination changed progress'
    run(exe, 'success', [click(startup, 'nav-0'), click(maps, 'map-1'), 'dump tree success.json', 'dump pixels success.pixels'])
    success = tree('success'); selector(success, range(1,21))
    state = json.loads((OUT/'save/hero-v2.json').read_text(encoding='utf-8-sig'))
    assert state['floor'] == 1 and state['best'] == 10 and state['active'] == 0
    run(exe, 'station', [click(startup, 'nav-0'), 'dump tree stationed.json'], fresh=False)
    stationed = tree('stationed'); selector(stationed, range(1,21))
    node(stationed, 'map-return')
    state = json.loads((OUT/'save/hero-v2.json').read_text(encoding='utf-8-sig'))
    assert state['travelMap'] == 0 and state['homeFloor'] == 6 and state['best'] == 10, 'Station and home track must persist'
    run(exe, 'return', [click(startup, 'nav-0'), click(stationed, 'map-return'), 'dump tree returned-home.json'], fresh=False)
    returned_home = tree('returned-home'); selector(returned_home, range(1,21))
    assert not any(n.get('name') == 'map-return' for n in returned_home), 'Return button must hide back home'
    state = json.loads((OUT/'save/hero-v2.json').read_text(encoding='utf-8-sig'))
    assert state['travelMap'] == -1 and state['floor'] == 6, 'Return restores the illusion track'
    run(exe, 'navigation', [click(startup, 'nav-0'), click(maps, 'nav-2'), 'dump tree illusion.json', click(maps, 'nav-0'), 'dump tree returned.json', click(maps, 'map-next'), 'dump tree page-two.json'])
    node(tree('illusion'), 'challenge'); assert not any(n.get('name') == 'map-selector' for n in tree('illusion'))
    selector(tree('returned'), range(1,21)); two = tree('page-two'); selector(two, range(21,36))
    run(exe, 'pagination', [click(startup, 'nav-0'), click(maps, 'map-next'), click(two, 'map-prev'), 'dump tree page-one.json'])
    selector(tree('page-one'), range(1,21))
    print('PASS real map routing: entry, locked destination, successful travel/save, stationed track preservation, return home, illusion/return nav, both pages (35 destinations)')

if __name__ == '__main__':
    main()
