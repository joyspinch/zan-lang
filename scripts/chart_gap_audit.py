#!/usr/bin/env python3
"""Code-level completeness audit: Zan chart engine vs the ECharts option corpus.

Answers two questions the screenshot audits cannot:

  1. Which option keys does the official corpus set that the engine NEVER reads?
     An option key the engine never looks up is silently dropped -- the demo
     cannot be right no matter how the pixels happen to land.

  2. How much of the engine was actually ported from the ECharts source?
     The engine cites an ECharts source file (e.g. ``poly.ts:152``) when a
     function was transcribed from it. Files with no citation were written
     against remembered/2.x behaviour and need a line-by-line comparison.

Usage:
    python scripts/chart_gap_audit.py            # human summary
    python scripts/chart_gap_audit.py --json out.json
    python scripts/chart_gap_audit.py --markdown out.md

Requires no ECharts checkout: it compares the corpus already committed under
``examples/gui_charts/options`` against ``stdlib/Gui/Component/Chart``.
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPTIONS = os.path.join(ROOT, 'examples', 'gui_charts', 'options')
ENGINE = os.path.join(ROOT, 'packages', 'Zan.Gui.Charts', 'src', 'Gui', 'Component', 'Chart')
if not os.path.exists(ENGINE):
    ENGINE = os.path.join(ROOT, 'stdlib', 'Gui', 'Component', 'Chart')

# Subtrees that hold data payload rather than option config. Their own key is a
# config key (`data`, `children`, ...), but the objects inside them are points,
# not options, so the walk does not descend.
DATA_STOP = {
    'data', 'children', 'nodes', 'links', 'edges', 'source', 'target',
    'coords', 'regions', 'styleJson', 'stylers', 'features', 'geometry',
    'coordinates', 'matrix', 'points', 'nameMap', 'colorStops',
}

ACCESSORS = ('Get', 'Int', 'Str', 'Bool', 'Num', 'Double', 'Has')
SRC_CITE = re.compile(r'[A-Za-z_][A-Za-z0-9_]*\.ts(?::\d+)?')


def config_keys(obj, path=()):
    """Yield (key, path) for keys in option-config position."""
    if isinstance(obj, dict):
        for k, v in obj.items():
            yield k, path + (k,)
            if k in DATA_STOP:
                continue
            yield from config_keys(v, path + (k,))
    elif isinstance(obj, list):
        for v in obj:
            yield from config_keys(v, path)


def load_engine():
    src = {}
    for p in sorted(glob.glob(os.path.join(ENGINE, '*.zan'))):
        src[os.path.basename(p)] = open(p, encoding='utf-8', errors='replace').read()
    return src


def read_keys(engine_src):
    """Every quoted key the engine looks up through a Json accessor."""
    whole = '\n'.join(engine_src.values())
    keys = set()
    for acc in ACCESSORS:
        keys.update(re.findall(r'\.%s\("([^"]+)"' % acc, whole))
    return keys


def scan():
    engine_src = load_engine()
    read = read_keys(engine_src)

    key_files = collections.defaultdict(set)
    n_files = 0
    for f in sorted(glob.glob(os.path.join(OPTIONS, '*.json'))):
        n_files += 1
        ident = os.path.basename(f)[:-5]
        try:
            obj = json.load(open(f, encoding='utf-8'))
        except Exception as exc:  # a broken option file is itself a finding
            print('PARSE FAIL %s: %s' % (ident, exc), file=sys.stderr)
            continue
        for k, _path in config_keys(obj):
            key_files[k].add(ident)

    unread = {k: v for k, v in key_files.items() if k not in read}

    cites = {}
    for name, text in engine_src.items():
        found = SRC_CITE.findall(text)
        if found:
            cites[name] = (len(text.splitlines()), sorted(set(found)))
    return n_files, key_files, unread, cites, engine_src


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--json')
    ap.add_argument('--markdown')
    ap.add_argument('--top', type=int, default=0,
                    help='limit unread keys listed (0 = all)')
    ap.add_argument('--write-baseline', metavar='PATH',
                    help='record the current gaps as the ratchet baseline')
    ap.add_argument('--check', metavar='PATH',
                    help='fail (exit 1) if new gaps appeared vs PATH')
    args = ap.parse_args()

    n_files, key_files, unread, cites, engine_src = scan()
    total_keys = len(key_files)
    total_lines = sum(len(t.splitlines()) for t in engine_src.values())
    cited_lines = sum(v[0] for v in cites.values())

    if args.write_baseline:
        json.dump({
            'note': 'Ratchet baseline. New unread keys fail --check; remove keys '
                    'as they are implemented. Regenerate deliberately.',
            'unread_keys': sorted(unread),
            'source_citations': {n: v[1] for n, v in cites.items()},
        }, open(args.write_baseline, 'w', encoding='utf-8'),
            ensure_ascii=False, indent=1)
        print('wrote baseline: %d unread keys -> %s'
              % (len(unread), args.write_baseline))
        return 0

    if args.check:
        base = json.load(open(args.check, encoding='utf-8'))
        known = set(base.get('unread_keys', []))
        new = sorted(set(unread) - known)
        # Citations are the audit trail for source-faithful ports; losing one
        # means a ported function was rewritten without its provenance.
        lost = []
        for name, files in base.get('source_citations', {}).items():
            now = set(cites.get(name, (0, []))[1])
            gone = sorted(set(files) - now)
            if gone:
                lost.append('%s: %s' % (name, ', '.join(gone)))
        if not new and not lost:
            print('ratchet OK: no new unread keys, no lost source citations '
                  '(%d unread keys still tracked)' % len(unread))
            return 0
        if new:
            print('FAIL: %d option key(s) the engine never reads appeared:'
                  % len(new))
            for k in new:
                print('   %-28s %s' % (k, ', '.join(sorted(unread[k])[:3])))
        if lost:
            print('FAIL: source-citation provenance dropped:')
            for line in lost:
                print('   ' + line)
        print('\nEither implement the key, or add it to the baseline with '
              '--write-baseline and say why in docs/CHART_CODE_GAP_LEDGER.md.')
        return 1

    print('option files scanned      : %d' % n_files)
    print('distinct config keys      : %d' % total_keys)
    print('keys the engine never reads: %d (%.0f%%)'
          % (len(unread), 100.0 * len(unread) / max(1, total_keys)))
    print('engine files              : %d (%d lines)'
          % (len(engine_src), total_lines))
    print('files citing ECharts src  : %d (%d lines, %.0f%%)'
          % (len(cites), cited_lines, 100.0 * cited_lines / max(1, total_lines)))
    print()
    print('--- unread keys, widest blast radius first ---')
    ordered = sorted(unread.items(), key=lambda kv: (-len(kv[1]), kv[0]))
    for k, fs in (ordered[:args.top] if args.top else ordered):
        sample = ','.join(sorted(fs)[:3])
        print('  %-28s %3d files   %s' % (k, len(fs), sample))
    print()
    print('--- engine files citing the ECharts source ---')
    for name in sorted(cites, key=lambda n: -cites[n][0]):
        lines, files = cites[name]
        print('  %-24s %5d lines   %s' % (name, lines, ', '.join(files)))

    if args.json:
        json.dump({
            'option_files': n_files,
            'distinct_config_keys': total_keys,
            'unread_keys': {k: sorted(v) for k, v in ordered},
            'source_citations': {n: {'lines': v[0], 'cites': v[1]}
                                 for n, v in cites.items()},
        }, open(args.json, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)

    if args.markdown:
        with open(args.markdown, 'w', encoding='utf-8') as fh:
            fh.write('# chart code-level gap audit (generated)\n\n')
            fh.write('Generated by `scripts/chart_gap_audit.py`.\n\n')
            fh.write('| metric | value |\n|---|---|\n')
            fh.write('| option files | %d |\n' % n_files)
            fh.write('| distinct config keys | %d |\n' % total_keys)
            fh.write('| keys never read by engine | %d |\n' % len(unread))
            fh.write('| engine lines | %d |\n' % total_lines)
            fh.write('| engine lines in files citing ECharts src | %d |\n'
                     % cited_lines)
            fh.write('\n## Keys the engine never reads\n\n| key | files | example |\n')
            fh.write('|---|---|---|\n')
            for k, fs in ordered:
                fh.write('| `%s` | %d | %s |\n'
                         % (k, len(fs), ', '.join(sorted(fs)[:2])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
