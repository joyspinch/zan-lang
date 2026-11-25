"""S5（docs/ZAN_STANDARD.md §1）：scripts/zan_check.py 单份 JSON 报告的契约。

复用已有夹具：干净程序全过；孤儿环程序在 leaks 步失败并带出环路径（strict）；
缺分号程序在 compile 步失败、带 E0032 与 fix-it，后续 leaks 记 skipped；
Environment.Exit 时栈帧/静态持有不算泄漏；--no-run 跳过运行。ui/replay 恒为
skipped（P2/P3 未实现），报告形状从第一天起稳定。"""
import argparse
import json
import os
import subprocess
import sys


def check(root, zanc, *args):
    r = subprocess.run([sys.executable, '-I', '-B', os.path.join(root, 'scripts', 'zan_check.py'),
                        '--zanc', zanc, *args], cwd=root, capture_output=True, text=True,
                       timeout=600)
    report = json.loads(r.stdout)
    steps = {s['name']: s for s in report['steps']}
    assert [s['name'] for s in report['steps']] == ['compile', 'leaks', 'ui', 'replay'], report
    assert steps['ui']['status'] == 'skipped' and steps['replay']['status'] == 'skipped'
    assert (r.returncode == 0) == report['ok'], (r.returncode, report['ok'])
    return report, steps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zanc', required=True)
    ap.add_argument('--root', required=True)
    a = ap.parse_args()
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
    print('zan check: report contracts passed')


if __name__ == '__main__':
    main()
