"""S20（docs/ZAN_STANDARD.md §5）：窗体标记参与编译的契约。

tests/diag/markup/TypoForm.zan 的同名 TypoForm.html 里写错了处理器名、绑定路径
（含下钻到用户类的第二段）与挂载字段：编译期必须逐条报在 .html 的属性值上
（诊断码、行列、区间正好盖住写错的名字），补丁是拼写最近的成员；把补丁按 offset
应用回 .html 后再编译必须通过。夹具里不该报的写法（注释、路由动作、带参调用、
模板行内绑定、字符串登记的处理器、非窗体类、自定义 ViewModel 的子窗）
一条都不能报。另验 --embed 配对（标记不在源文件旁边、经 --embed 给出时同样检查），
以及 ExprForm 的 E2115：绑定值是表达式/字面量、属性名带连字符（运行期建树会抛异常）。"""
import argparse
import json
import os
import shutil
import subprocess

WANT = [
    # (码, 行, 写错的名字, 补丁)
    ('E2113', 4, 'usr', 'user'),
    ('E2112', 5, 'OnLgin', 'OnLogin'),
    ('E2113', 9, 'showEror', 'showError'),
    ('E2113', 10, 'cty', 'city'),
    ('E2114', 12, 'slott', 'slot'),
]


def check(zanc, cwd, args):
    r = subprocess.run([zanc] + args + ['--auto-stdlib', '--diag=json', '--summary'],
                       cwd=cwd, capture_output=True, text=True, timeout=300)
    diags = []
    for line in r.stderr.splitlines():
        try:
            o = json.loads(line)
        except ValueError:
            continue
        if o.get('kind') == 'diagnostic' and o.get('severity') == 'error':
            diags.append(o)
    return r.returncode, diags


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zanc', required=True)
    ap.add_argument('--root', required=True)
    ap.add_argument('--work', required=True)
    a = ap.parse_args()
    zanc = os.path.realpath(a.zanc)
    fixture = os.path.join(os.path.realpath(a.root), 'tests', 'diag', 'markup')
    work = os.path.realpath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(fixture, work)

    rc, diags = check(zanc, work, ['TypoForm.zan'])
    assert rc != 0, 'expected markup errors'
    html = open(os.path.join(work, 'TypoForm.html'), 'rb').read()
    got = []
    for d in diags:
        assert d['file'].endswith('TypoForm.html'), d
        name = html[d['offset']:d['end']['offset']].decode('utf-8')
        fixes = d.get('fixits', [])
        assert len(fixes) == 1 and fixes[0]['offset'] == d['offset'] \
            and fixes[0]['length'] == len(name.encode('utf-8')), d
        got.append((d['code'], d['line'], name, fixes[0]['text']))
    assert got == WANT, (got, [d['message'] for d in diags])

    for f in sorted((f for d in diags for f in d['fixits']), key=lambda f: f['offset'], reverse=True):
        html = html[:f['offset']] + f['text'].encode('utf-8') + html[f['offset'] + f['length']:]
    open(os.path.join(work, 'TypoForm.html'), 'wb').write(html)
    rc, diags = check(zanc, work, ['TypoForm.zan'])
    assert rc == 0 and not diags, ('fixed markup still fails', [d['message'] for d in diags])

    # --embed 配对：标记不在源文件旁边
    os.makedirs(os.path.join(work, 'ui'))
    shutil.copy(os.path.join(fixture, 'TypoForm.html'), os.path.join(work, 'ui', 'TypoForm.html'))
    os.remove(os.path.join(work, 'TypoForm.html'))
    rc, diags = check(zanc, work, ['TypoForm.zan'])
    assert rc == 0 and not diags, ('no markup beside the source: nothing to check', diags)
    rc, diags = check(zanc, work, ['TypoForm.zan', '--embed', 'ui/TypoForm.html=TypoForm.html'])
    assert rc != 0 and [d['code'] for d in diags] == [w[0] for w in WANT], \
        [d['message'] for d in diags]
    # E2115：表达式/字面量/带连字符的属性名/空段——运行期建树时 DeclareBinding 会抛异常
    rc, diags = check(zanc, work, ['ExprForm.zan'])
    got = [(d['code'], d['line']) for d in diags]
    assert rc != 0 and got == [('E2115', 2), ('E2115', 3), ('E2115', 4), ('E2115', 5)], \
        [d['message'] for d in diags]
    assert not any(d.get('fixits') for d in diags), diags
    print('markup check: %d typos reported with fix-its, fixed markup compiles, --embed paired,'
          ' %d invalid bindings rejected' % (len(WANT), len(got)))


if __name__ == '__main__':
    main()
