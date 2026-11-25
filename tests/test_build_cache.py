"""Build cache (docs/BUILD_CACHE.md stage 1): LLVM backend memoization.

Usage: python -I test_build_cache.py --zanc <path> [--work <dir>]

Pins: a repeat build hits both memo tables and links a byte-identical
executable; --no-build-cache / ZAN_BUILD_CACHE=0 / ZAN_DUMP_OPT_IR switch the
cache off with identical output; a source edit, a different -O level and a
corrupt entry all recompute instead of returning stale code; a split
(multi-object) codegen entry round-trips.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

SRC = '''using System;
using System.Collections.Generic;

class Shape {
    public virtual string Name() { return "shape"; }
}

class Square : Shape {
    int side;
    public Square(int s) { side = s; }
    public override string Name() { return "square " + Convert.ToString(side * side); }
}

class P {
    static void Main() {
        List<Shape> all = new List<Shape>();
        for (int i = 1; i <= 3; i++) all.Add(new Square(i));
        foreach (Shape s in all) Console.WriteLine(s.Name());
        Console.WriteLine("MARK");
    }
}
'''

STAT = re.compile(r'build cache: optimize (\d+) hit/(\d+) miss, object (\d+) hit/(\d+) miss')
failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print('FAIL: ' + what)


def build(zanc, src, exe, cache, extra=(), env_extra=None):
    env = os.environ.copy()
    for k in ('ZAN_BUILD_CACHE', 'ZAN_DUMP_OPT_IR', 'ZAN_LLVM_ARGS', 'ZAN_CODEGEN_SPLIT'):
        env.pop(k, None)
    env['ZAN_CACHE_DIR'] = str(cache)
    if env_extra:
        env.update(env_extra)
    r = subprocess.run([str(zanc), str(src), '--auto-stdlib', '--time', '-o', str(exe),
                        *extra], env=env, capture_output=True, timeout=300)
    err = r.stderr.decode('utf-8', errors='replace')
    if r.returncode != 0:
        print(err)
        raise SystemExit(f'build failed: {extra} {env_extra}')
    m = STAT.search(err)
    stats = tuple(int(x) for x in m.groups()) if m else None
    return stats, ('build cache: off' in err), err


def run_exe(exe):
    r = subprocess.run([str(exe)], capture_output=True, timeout=60)
    return r.returncode, r.stdout.decode('utf-8', errors='replace').replace('\r\n', '\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zanc', type=Path, required=True)
    ap.add_argument('--work', type=Path, default=Path('build/test-out/build-cache'))
    args = ap.parse_args()
    zanc = args.zanc.resolve()
    work = args.work.resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    cache = work / 'cache'
    src = work / 'memo.zan'
    src.write_text(SRC, encoding='utf-8')
    ext = '.exe' if sys.platform == 'win32' else ''
    exe = work / ('memo' + ext)
    want = 'square 1\nsquare 4\nsquare 9\nMARK\n'

    # 1. cold: both tables miss and are filled
    st, _, _ = build(zanc, src, exe, cache)
    check(st == (0, 1, 0, 1), f'cold build stats {st}')
    check(run_exe(exe) == (0, want), 'cold build output')
    cold = exe.read_bytes()

    # 2. warm: both hit, report replayed, identical executable
    st, _, err = build(zanc, src, exe, cache)
    check(st == (1, 0, 1, 0), f'warm build stats {st}')
    check('Optimization report' in err, 'optimizer report replayed on hit')
    check(exe.read_bytes() == cold, 'warm executable identical to cold')
    check(run_exe(exe) == (0, want), 'warm build output')

    # 3. off switches: same bytes, no cache line
    for extra, env_extra, tag in (
            (('--no-build-cache',), None, '--no-build-cache'),
            ((), {'ZAN_BUILD_CACHE': '0'}, 'ZAN_BUILD_CACHE=0'),
            ((), {'ZAN_DUMP_OPT_IR': str(work / 'dump.ll')}, 'ZAN_DUMP_OPT_IR')):
        st, off, _ = build(zanc, src, exe, cache, extra, env_extra)
        check(off and st is None, f'{tag} turns the cache off')
        check(exe.read_bytes() == cold, f'{tag} executable identical to cached')
    check((work / 'dump.ll').exists(), 'ZAN_DUMP_OPT_IR still dumps (not swallowed by a hit)')

    # 4. a different optimization level is a different key
    st, _, _ = build(zanc, src, exe, cache, ('-O1',))
    check(st is not None and st[0] == 0 and st[1] == 1, f'-O1 misses the optimize table {st}')
    check(run_exe(exe) == (0, want), '-O1 output')

    # 5. a source edit never returns stale code
    src.write_text(SRC.replace('"MARK"', '"EDITED"'), encoding='utf-8')
    st, _, _ = build(zanc, src, exe, cache)
    check(st == (0, 1, 0, 1), f'edited source misses {st}')
    check(run_exe(exe) == (0, want.replace('MARK', 'EDITED')), 'edited source output')
    src.write_text(SRC, encoding='utf-8')

    # 6. a corrupt entry is recomputed (and rewritten), not trusted
    for f in (cache / 'backend').glob('llvmcache-zan-*.opt.bc'):
        f.write_bytes(b'BC\xc0\xde' + b'\0' * 16)
    st, _, _ = build(zanc, src, exe, cache)
    check(st is not None and st[1] == 1, f'corrupt optimize entry recomputed {st}')
    check(exe.read_bytes() == cold, 'rebuild after corruption identical to cold')
    st, _, _ = build(zanc, src, exe, cache)
    check(st == (1, 0, 1, 0), f'rewritten entry hits again {st}')

    # 7. split codegen: an N-object entry round-trips
    split = {'ZAN_CODEGEN_SPLIT': '3'}
    st, _, _ = build(zanc, src, exe, cache, (), split)
    check(st is not None and st[3] == 1, f'split build fills the object table {st}')
    split_cold = exe.read_bytes()
    st, _, _ = build(zanc, src, exe, cache, (), split)
    check(st == (1, 0, 1, 0), f'split build hits {st}')
    check(exe.read_bytes() == split_cold, 'split warm executable identical')
    check(run_exe(exe) == (0, want), 'split build output')

    if failures:
        print(f'build cache: {len(failures)} check(s) failed')
        return 1
    print('build cache memo: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
