"""S2（docs/ZAN_STANDARD.md §1）：zanc --query 的 JSON Lines 契约。

夹具 tests/query/query_store.zan：Store.Load 直接抛 ArgumentException，经 Check()
可达 InvalidOperationException，经 File.ReadAllText 用到 File 能力（并可达
FileNotFoundException），lambda 捕获 this 与形参 path，Main 里两个调用点。
另核对单名按类型名优先（Store 列出它的方法，而不是 Vector128.Store）、
无匹配退出码 1。"""
import argparse
import json
import os
import subprocess


def query(zanc, src, name):
    r = subprocess.run([zanc, os.path.basename(src), '--auto-stdlib', '--query', name],
                       cwd=os.path.dirname(src), capture_output=True, text=True, timeout=300)
    lines = [json.loads(l) for l in r.stdout.splitlines() if l.strip()]
    assert lines and lines[-1]['kind'] == 'summary', r.stdout + r.stderr
    return r.returncode, [l for l in lines if l['kind'] == 'query'], lines[-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zanc', required=True)
    ap.add_argument('--root', required=True)
    a = ap.parse_args()
    a.zanc = os.path.realpath(a.zanc)
    src = os.path.join(os.path.realpath(a.root), 'tests', 'query', 'query_store.zan')

    rc, hits, summary = query(a.zanc, src, 'Store.Load')
    assert rc == 0 and summary['matches'] == 1 and len(hits) == 1, (rc, summary)
    q = hits[0]
    assert q['symbol'] == 'Store.Load(string path)' and q['line'] == 11, q['symbol']
    assert {'what': 'ArgumentException', 'line': 12} in q['allocates'], q['allocates']
    assert any(x['what'] == 'closure' for x in q['allocates']), q['allocates']
    assert q['captures'] == [{'line': 14, 'this': True, 'locals': ['path']}], q['captures']
    assert q['throws']['direct'] == [{'type': 'ArgumentException', 'line': 12}], q['throws']
    reach = {t['type']: t['via'] for t in q['throws']['reachable']}
    assert reach.get('InvalidOperationException') == 'Store.Load(string path) -> Store.Check()', reach
    assert 'FileNotFoundException' in reach, reach
    caps = {c['capability']: c['via'] for c in q['capabilities']}
    assert list(caps) == ['File'], caps
    assert caps['File'].endswith('File.ReadAllText(string path)'), caps
    assert 'Store.Check()' in q['calls'], q['calls']
    callers = [(c['caller'], c['line']) for c in q['callers']]
    assert callers == [('Program.Main()', 25), ('Program.Main()', 26)], callers
    assert q['callers_truncated'] is False

    rc, hits, summary = query(a.zanc, src, 'Store')
    assert rc == 0 and {h['symbol'] for h in hits} == {'Store.Load(string path)', 'Store.Check()'}, hits

    rc, hits, summary = query(a.zanc, src, 'Store.Nope')
    assert rc == 1 and summary['matches'] == 0 and not hits, summary
    print('query: JSON contracts passed')


if __name__ == '__main__':
    main()
