"""P2 / P3 验收（docs/ZAN_STANDARD.md §8）：模板通过 zan-check 的对应步骤。

--family gui（默认）：全部 templates/gui 窗体通过 ui 步（S22）；
--family game：全部 templates/game 的 replays/*.replay 重放一致、机器人模糊不违反
[Invariant]（replay 步，S26/S27）——每个游戏模板都必须带回放（没有即失败）。

每个模板按 IDE 新建工程的方式脚手架（ZanIDE.Scaffold：{{NAME}} 替换成工程名，
src/App.zan / App.html 改名为 <工程名>.zan / .html），再跑
`zan-check src --no-run`：ui 步须 pass（100/150/200% DPI 下无截断、过小目标、重叠、
出界、塌缩）；没有声明式窗体的模板（自己装载标记、或没有 Form）记为 skipped。
整档 full 标签：每个模板要编两次（程序 + 检查程序），全量约一小时。"""
import argparse
import json
import os
import shutil
import subprocess
import sys

NAME = 'Demo'


def scaffold(src, dst):
    shutil.copytree(src, dst)
    for root, _, files in os.walk(dst):
        for fn in files:
            p = os.path.join(root, fn)
            if fn == 'template.manifest':
                os.remove(p)
                continue
            try:
                text = open(p, encoding='utf-8').read()
            except (UnicodeDecodeError, OSError):
                continue
            if '{{NAME}}' in text:
                open(p, 'w', encoding='utf-8').write(text.replace('{{NAME}}', NAME))
    for ext in ('.zan', '.html'):
        old = os.path.join(dst, 'src', 'App' + ext)
        if os.path.exists(old):
            os.rename(old, os.path.join(dst, 'src', NAME + ext))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zanc', required=True)
    ap.add_argument('--check', required=True)
    ap.add_argument('--drivers', required=True)
    ap.add_argument('--root', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--only', default='')
    ap.add_argument('--family', default='gui', choices=['gui', 'game'])
    a = ap.parse_args()
    zanc, check, drivers = (os.path.realpath(x) for x in (a.zanc, a.check, a.drivers))
    tdir = os.path.join(os.path.realpath(a.root), 'templates', a.family)
    step = 'ui' if a.family == 'gui' else 'replay'
    skip = ['--no-replay'] if step == 'ui' else ['--no-ui']
    work = os.path.realpath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    failed = []
    passed = skipped = 0
    for name in sorted(os.listdir(tdir)):
        if a.only and name != a.only:
            continue
        src = os.path.join(tdir, name)
        if not os.path.isfile(os.path.join(src, 'template.manifest')):
            continue
        dst = os.path.join(work, name)
        scaffold(src, dst)
        report = os.path.join(work, name + '.json')
        subprocess.run([check, 'src', '--zanc', zanc, '--no-run', *skip, '--zanc-arg',
                        '--driver-dir', '--zanc-arg', drivers, '--out', report],
                       cwd=dst, capture_output=True, text=True, timeout=3000)
        steps = {s['name']: s for s in json.load(open(report, encoding='utf-8'))['steps']}
        ui = steps.get(step, {})
        status = ui.get('status')
        if step == 'replay' and status == 'skipped':
            status = 'missing'   # 游戏模板必须带回放
        if steps.get('compile', {}).get('status') != 'pass':
            failed.append((name, 'compile failed'))
        elif status == 'pass':
            passed += 1
        elif status == 'skipped':
            skipped += 1
            print('%s: skipped (%s)' % (name, ui.get('reason', '')))
        elif step == 'replay':
            bad = [(r['kind'], r['seed'], r['result']) for r in ui.get('runs', []) if not r['ok']]
            failed.append((name, bad or ui.get('reason') or ui.get('output_tail')))
        else:
            real = [(c['scale'], [(i['kind'], i['control'], i['detail']) for i in c['issues']])
                    for c in ui.get('checks', []) if c['issues'] and not c['pseudo']]
            failed.append((name, real or ui.get('output_tail')))
    for name, why in failed:
        print('FAIL %s: %s' % (name, why))
    print('template %s: %d passed, %d skipped, %d failed' % (step, passed, skipped, len(failed)))
    sys.exit(1 if failed or passed == 0 else 0)


if __name__ == '__main__':
    main()
