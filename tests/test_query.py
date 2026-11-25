"""S2（docs/ZAN_STANDARD.md §1）：zanc --query 的 JSON Lines 契约。

夹具 tests/query/query_store.zan：Store.Load 直接抛 ArgumentException，经 Check()
逃出 InvalidOperationException，经 File.ReadAllText 用到 File 能力（并逃出
FileNotFoundException），lambda 捕获 this 与形参 path，Main 里两个调用点。
夹具 tests/query/query_catch.zan：catch 扣除（具体/基类型/裸 catch 收住，rethrow 与
catch 体里新抛的逃出，lambda 体不算外层方法会抛，互递归各算各的 catch）。
夹具 tests/query/query_virtual.zan：接口调用连到实现、抽象调用连到 override、
方法组当值用算能力不算抛出。
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
    esc = {t['type']: t['via'] for t in q['throws']['escapes']}
    assert esc.get('InvalidOperationException') == 'Store.Load(string path) -> Store.Check()', esc
    assert esc.get('ArgumentException') == 'Store.Load(string path)', esc
    assert esc.get('FileNotFoundException', '').endswith('File.ReadAllText(string path)'), esc
    assert q['throws']['escapes_truncated'] is False
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

    csrc = os.path.join(os.path.dirname(src), 'query_catch.zan')
    want = {
        'Specific': ([], []),
        'Base': ([], []),
        'All': ([], []),
        'Rethrow': (['(rethrow)'], ['FileNotFoundException']),
        'InCatch': (['ArgumentException'], ['ArgumentException']),
        'Later': ([], []),
        'A': ([], []),
        'B': (['InvalidOperationException'], ['InvalidOperationException']),
    }
    for name, (direct, escapes) in want.items():
        rc, hits, _ = query(a.zanc, csrc, 'Svc.' + name)
        assert rc == 0 and len(hits) == 1, (name, hits)
        t = hits[0]['throws']
        got = ([d['type'] for d in t['direct']], sorted(e['type'] for e in t['escapes']))
        assert got == (direct, sorted(escapes)), (name, got)
    rc, hits, _ = query(a.zanc, csrc, 'Svc.Rethrow')
    via = hits[0]['throws']['escapes'][0]['via']
    assert via == 'Svc.Rethrow(string p) -> File.ReadAllText(string path)', via
    rc, hits, _ = query(a.zanc, csrc, 'Svc.Specific')
    assert [c['capability'] for c in hits[0]['capabilities']] == ['File'], hits[0]['capabilities']

    vsrc = os.path.join(os.path.dirname(src), 'query_virtual.zan')
    rc, hits, _ = query(a.zanc, vsrc, 'Program.Use')
    q = hits[0]
    assert 'FileStore.Load(string p)' in q['calls'], q['calls']
    assert [e['type'] for e in q['throws']['escapes']] == ['FileNotFoundException'], q['throws']
    assert q['capabilities'][0]['via'] == ('Program.Use(IStore s) -> FileStore.Load(string p)'
                                           ' -> File.ReadAllText(string path)'), q['capabilities']
    rc, hits, _ = query(a.zanc, vsrc, 'Shape.Twice')
    assert [e['type'] for e in hits[0]['throws']['escapes']] == ['InvalidOperationException'], hits
    rc, hits, _ = query(a.zanc, vsrc, 'Ui.Wire')
    q = hits[0]
    assert q['throws']['escapes'] == [] and q['calls'] == ['Ui.OnClick()'], q
    assert [c['capability'] for c in q['capabilities']] == ['File'], q['capabilities']
    print('query: JSON contracts passed')


if __name__ == '__main__':
    main()
