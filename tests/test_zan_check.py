"""S5（docs/ZAN_STANDARD.md §1）：原生 zan-check（src/check/ZanCheck.zan）单份 JSON 报告的契约。

复用已有夹具：干净程序全过；孤儿环程序在 leaks 步失败并带出环路径（strict）；
缺分号程序在 compile 步失败、带 E0032 与 fix-it，后续 leaks 记 skipped；
Environment.Exit 时栈帧/静态持有不算泄漏；--no-run 跳过运行；永不退出的程序在
--timeout 到点后判失败；ui 步（S22）对输入里带同名 .html 的窗体做布局检查——反例按
种类报出截断/过小/重叠/出界/塌缩并判失败，正例三档 DPI 都通过，无窗体时 skipped。
replay 恒为
skipped（P2/P3 未实现），报告形状从第一天起稳定。"""
import argparse
import json
import os
import subprocess
import sys

CHECK_EXE = None


def check(root, zanc, *args):
    r = subprocess.run([CHECK_EXE, '--zanc', zanc, *args], cwd=root, capture_output=True,
                       text=True, timeout=600)
    report = json.loads(r.stdout)
    steps = {s['name']: s for s in report['steps']}
    assert [s['name'] for s in report['steps']] == ['compile', 'leaks', 'ui', 'replay'], report
    assert steps['replay']['status'] == 'skipped'
    assert (r.returncode == 0) == report['ok'], (r.returncode, report['ok'])
    return report, steps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zanc', required=True)
    ap.add_argument('--root', required=True)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--drivers', required=True)
    a = ap.parse_args()
    global CHECK_EXE
    CHECK_EXE = os.path.realpath(a.exe)
    a.zanc = os.path.realpath(a.zanc)
    a.drivers = os.path.realpath(a.drivers)
    root = os.path.realpath(a.root)

    rep, st = check(root, a.zanc, 'tests/affected/affected_fixture.zan')
    assert rep['ok'] and st['compile']['status'] == 'pass' and st['leaks']['leaked'] == 0, rep

    rep, st = check(root, a.zanc, 'tests/leak/leak_cycle_report.zan')
    lk = st['leaks']
    assert not rep['ok'] and lk['status'] == 'fail' and lk['mode'] == 'strict', lk
    assert lk['leaked'] == 10 and len(lk['cycles']) >= 2 and lk['sites'], lk
    assert any('.this -> LeakSub@' in c for c in lk['cycles']), lk['cycles']

    rep, st = check(root, a.zanc, 'tests/diag/diag_json_missing_semicolon.zan')
    cp = st['compile']
    assert cp['status'] == 'fail' and cp['errors'] == 1, cp
    assert cp['diagnostics'][0]['code'] == 'E0032' and cp['diagnostics'][0]['fixits'], cp
    assert st['leaks']['status'] == 'skipped', st['leaks']

    rep, st = check(root, a.zanc, 'tests/leak/leak_exit_roots.zan')
    assert rep['ok'] and st['leaks']['leaked'] == 0, rep

    rep, st = check(root, a.zanc, '--no-run', 'tests/leak/leak_cycle_report.zan')
    assert rep['ok'] and st['leaks']['status'] == 'skipped', rep

    rep, st = check(root, a.zanc, '--timeout', '2', 'tests/check/zan_check_hang.zan')
    assert not rep['ok'] and st['leaks']['status'] == 'fail', st['leaks']
    assert 'did not exit within 2s' in st['leaks']['reason'], st['leaks']
    assert st['ui']['status'] == 'skipped', st['ui']

    gui = ['--zanc-arg', '--driver-dir', '--zanc-arg', a.drivers,
           '--zanc-arg', '--package-project', '--zanc-arg', root]
    rep, st = check(root, a.zanc, *gui, 'tests/check/ui_bad/BadForm.zan')
    ui = st['ui']
    assert not rep['ok'] and ui['status'] == 'fail' and ui['forms'] == ['BadForm'], ui
    assert len(ui['checks']) == 6, ui
    for c in ui['checks']:
        if c['pseudo']:
            continue
        kinds = sorted({(i['kind'], i['control']) for i in c['issues']})
        assert kinds == sorted({('truncated', 'LongLabel'), ('small_target', 'Tiny'),
                                ('truncated', 'x'), ('offscreen', 'Far'), ('collapsed', 'Far'),
                                ('overlap', 'Left')}), (c['scale'], kinds)
    rep, st = check(root, a.zanc, *gui, 'tests/check/ui_good/GoodForm.zan')
    ui = st['ui']
    assert ui['status'] == 'pass' and ui['issues'] == 0 and ui['forms'] == ['GoodForm'], ui
    print('zan check: report contracts passed')


if __name__ == '__main__':
    main()
