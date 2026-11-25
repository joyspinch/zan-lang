"""S1（docs/ZAN_STANDARD.md §1）：诊断 fix-it 的端到端契约。

每个夹具用 --diag=json 编译：核对诊断码与补丁文本，然后把全部补丁按 offset
（从后往前，保证前面的 offset 不失效）应用回源码再编译——必须成功，证明补丁
可以机器直接应用。反例夹具（两个同样近的候选）必须只报错、不给补丁。"""
import argparse
import json
import os
import subprocess
import sys

CASES = [
    # (夹具, 期望的 (诊断码, 补丁文本) 列表)
    ('fixit_member_typo.zan', [('E2085', 'Increment'), ('E2085', 'Count')]),
    ('fixit_type_typo.zan', [('E1005', 'Counter')]),
    ('fixit_assign_condition.zan', [('E2073', '==')]),
]


def compile_json(zanc, src, out):
    r = subprocess.run([zanc, src, '--auto-stdlib', '--diag=json', '-o', out],
                       capture_output=True, text=True, timeout=300)
    diags = []
    for line in r.stderr.splitlines():
        try:
            o = json.loads(line)
        except ValueError:
            continue
        if o.get('kind') == 'diagnostic':
            diags.append(o)
    return r.returncode, diags


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zanc', required=True)
    ap.add_argument('--root', required=True)
    ap.add_argument('--work', required=True)
    a = ap.parse_args()
    zanc = os.path.realpath(a.zanc)
    ddir = os.path.join(os.path.realpath(a.root), 'tests', 'diag')
    os.makedirs(a.work, exist_ok=True)
    exe = os.path.join(a.work, 'fixit_case')

    for name, want in CASES:
        src = os.path.join(ddir, name)
        rc, diags = compile_json(zanc, src, exe)
        assert rc != 0, name + ': expected a compile error'
        got = [(d['code'], f['text']) for d in diags for f in d.get('fixits', [])]
        assert got == want, (name, got, [d['message'] for d in diags])
        text = open(src, 'rb').read()
        fixes = sorted((f for d in diags for f in d.get('fixits', [])),
                       key=lambda f: f['offset'], reverse=True)
        for f in fixes:
            text = text[:f['offset']] + f['text'].encode('utf-8') + text[f['offset'] + f['length']:]
        fixed = os.path.join(a.work, name)
        open(fixed, 'wb').write(text)
        rc2, diags2 = compile_json(zanc, fixed, exe)
        assert rc2 == 0, (name, 'fixed source still fails', [d['message'] for d in diags2])

    rc, diags = compile_json(zanc, os.path.join(ddir, 'fixit_ambiguous.zan'), exe)
    assert rc != 0 and [d['code'] for d in diags] == ['E2085'], diags
    assert not diags[0].get('fixits') and 'did you mean' not in diags[0]['message'], diags
    print('diag fix-its: %d fixtures applied cleanly, ambiguity refused' % len(CASES))


if __name__ == '__main__':
    main()
