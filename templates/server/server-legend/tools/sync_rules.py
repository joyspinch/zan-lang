"""Copy the legend rule tables into the server template.

The single edit source for every gameplay number is
`templates/game/legend/data/*.csv` -- the client's offline simulator and the
server's Rules loader read the same schema, so they must read the same bytes.
The server template has to ship self-contained (a published `server-legend.exe`
cannot reach back into another template), so it carries a committed copy under
`data/rules/`.  This script is the only thing that writes that copy; the drift
is a visible failure via `tests/templates/legend/test_rules_sync.py`.

    python templates/server/server-legend/tools/sync_rules.py           # copy
    python templates/server/server-legend/tools/sync_rules.py --check   # verify only

`navigation.csv` and the art mapping tables are client presentation data and are
copied too -- the server uses them for nothing, but keeping the directory a
verbatim mirror means one comparison, not a hand-maintained include list.
"""
import argparse
import filecmp
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'templates' / 'game' / 'legend' / 'data'
DEST = Path(__file__).resolve().parents[1] / 'data' / 'rules'
# Runtime state written next to the tables by the client, never a rule table.
SKIP = {'server.txt', 'login.txt', 'README.md', '.gitignore'}


def tables():
    return sorted(p for p in SOURCE.glob('*.csv') if p.name not in SKIP)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true',
                    help='report drift and exit non-zero instead of copying')
    args = ap.parse_args()

    if not SOURCE.is_dir():
        print(f'source missing: {SOURCE}', file=sys.stderr)
        return 2
    DEST.mkdir(parents=True, exist_ok=True)

    stale = []
    for path in tables():
        target = DEST / path.name
        same = target.is_file() and filecmp.cmp(path, target, shallow=False)
        if same:
            continue
        if args.check:
            stale.append(path.name)
        else:
            shutil.copyfile(path, target)
            print(f'copied {path.name}')

    extra = [p.name for p in sorted(DEST.glob('*.csv'))
             if p.name not in {t.name for t in tables()}]
    if args.check:
        for name in extra:
            print(f'extra: {name} (no source table)')
        if stale or extra:
            for name in stale:
                print(f'drift: {name}')
            return 1
        print(f'up to date ({len(tables())} tables)')
        return 0

    for name in extra:
        (DEST / name).unlink()
        print(f'removed {name}')
    print(f'synced {len(tables())} tables -> {DEST}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
