#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""S5（docs/ZAN_STANDARD.md §1）：zan check —— 一条命令跑完，输出单份 JSON 报告。

步骤（任何一步失败，后续依赖它的步骤记为 skipped，报告照样完整输出）：
  1. compile  zanc <输入...> --auto-stdlib --check-leaks --diag=json：
              收集全部诊断（S1 JSON Lines，含 fix-it）与 summary 计数；
  2. leaks    运行产物（带超时），解析退出时的泄漏报告（S10/S13）：
              残留对象数、按分配站点的计数、逐条孤儿环路径，以及判定口径
              （Main 正常返回 = strict；Environment.Exit 等提前退出 = early-exit，
              只计不可达环）；程序退出码非 0 也记失败；
  3. ui       zan ui check（S22，P2）——未实现，skipped；
  4. replay   回放比对与不变量（S26/S27，P3）——未实现，skipped。
报告：{"version":1, "ok":bool, "inputs":[...], "steps":[{name,status,...}]}，
status ∈ pass | fail | skipped。退出码：0 全部通过（skipped 不算失败）；
1 有失败；2 参数/环境问题。

用法：
  python3 scripts/zan_check.py app.zan [more.zan|dir ...] [--zanc build/zanc]
         [--timeout 60] [--no-run] [--out report.json] [--zanc-arg X ...]
         [-- 程序参数...]
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

LEAK_HEADER = 'zan: memory leak detected:'
SITE_RE = re.compile(r'^\s+(\d+) object\(s\) leaked, allocated at (.*)$')
CYCLE_RE = re.compile(r'^\s+cycle: (.*)$')
COUNT_RE = re.compile(r'detected: (\d+) object\(s\)')


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def default_zanc(root):
    for cand in ('build/zanc', 'build/zanc.exe', 'toolchain/zanc', 'toolchain/zanc.exe'):
        p = os.path.join(root, cand)
        if os.path.isfile(p):
            return p
    return 'zanc'


def step_compile(zanc, inputs, extra, exe):
    cmd = [zanc, *inputs, '--auto-stdlib', '--check-leaks', '--diag=json', '-o', exe, *extra]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    diags, summary, other = [], None, []
    for line in r.stderr.splitlines():
        s = line.strip()
        if not s:
            continue
        try:
            obj = json.loads(s)
        except ValueError:
            other.append(s)
            continue
        if obj.get('kind') == 'diagnostic':
            diags.append(obj)
        elif obj.get('kind') == 'summary':
            summary = obj
        else:
            other.append(s)
    errors = summary['errors'] if summary else sum(
        1 for d in diags if d.get('severity') == 'error')
    warnings = summary['warnings'] if summary else sum(
        1 for d in diags if d.get('severity') == 'warning')
    ok = r.returncode == 0 and os.path.isfile(exe)
    out = {'name': 'compile', 'status': 'pass' if ok else 'fail',
           'exit_code': r.returncode, 'errors': errors, 'warnings': warnings,
           'diagnostics': diags}
    if other and not ok:
        out['other_output'] = other[-20:]   # 链接器等非 JSON 输出，失败时留尾巴
    return out


def parse_leaks(stdout):
    at = stdout.find(LEAK_HEADER)
    if at < 0:
        return None
    report = stdout[at:].splitlines()
    head = report[0]
    m = COUNT_RE.search(head)
    sites, cycles = [], []
    for line in report[1:]:
        sm = SITE_RE.match(line)
        if sm:
            sites.append({'count': int(sm.group(1)), 'site': sm.group(2)})
            continue
        cm = CYCLE_RE.match(line)
        if cm:
            cycles.append(cm.group(1))
    return {'leaked': int(m.group(1)) if m else None,
            'mode': 'early-exit' if 'unreachable cycles at exit' in head else 'strict',
            'header': head, 'sites': sites, 'cycles': cycles}


def step_leaks(exe, args, timeout, cwd):
    env = dict(os.environ)
    env['ZAN_LEAK_LOG'] = ''        # 只要 stdout 里的报告，不落盘
    try:
        r = subprocess.run([exe, *args], cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                           capture_output=True, text=True, encoding='utf-8',
                           errors='replace', timeout=timeout)
    except subprocess.TimeoutExpired:
        return {'name': 'leaks', 'status': 'fail', 'reason':
                'program did not exit within %ss (pass --timeout or --no-run)' % timeout}
    leak = parse_leaks(r.stdout)
    out = {'name': 'leaks', 'exit_code': r.returncode}
    if leak:
        out.update(leak)
        out['status'] = 'fail'
    else:
        out.update({'leaked': 0, 'status': 'pass'})
    if r.returncode != 0:
        out['status'] = 'fail'
        out['stderr_tail'] = r.stderr.splitlines()[-20:]
    return out


def main():
    argv = sys.argv[1:]
    prog_args = []
    if '--' in argv:
        i = argv.index('--')
        argv, prog_args = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description='zan check: compile + leak gate (+ui/replay) '
                                             'in one JSON report (S5)')
    ap.add_argument('inputs', nargs='+')
    ap.add_argument('--zanc')
    ap.add_argument('--zanc-arg', action='append', default=[])
    ap.add_argument('--timeout', type=float, default=60)
    ap.add_argument('--no-run', action='store_true')
    ap.add_argument('--out')
    ap.add_argument('--workdir')
    a = ap.parse_args(argv)

    root = repo_root()
    zanc = a.zanc or default_zanc(root)
    tmp = tempfile.mkdtemp(prefix='zan-check-')
    exe = os.path.join(tmp, 'app' + ('.exe' if os.name == 'nt' else ''))

    steps = [step_compile(zanc, a.inputs, a.zanc_arg, exe)]
    if steps[0]['status'] != 'pass':
        steps.append({'name': 'leaks', 'status': 'skipped', 'reason': 'compile failed'})
    elif a.no_run:
        steps.append({'name': 'leaks', 'status': 'skipped', 'reason': '--no-run'})
    else:
        steps.append(step_leaks(exe, prog_args, a.timeout, a.workdir or os.getcwd()))
    steps.append({'name': 'ui', 'status': 'skipped',
                  'reason': 'zan ui check (S22, ZAN_STANDARD P2) is not implemented yet'})
    steps.append({'name': 'replay', 'status': 'skipped',
                  'reason': 'replay and invariants (S26/S27, ZAN_STANDARD P3) are not implemented yet'})

    ok = all(s['status'] != 'fail' for s in steps)
    report = {'version': 1, 'ok': ok, 'inputs': a.inputs, 'steps': steps}
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if a.out:
        with open(a.out, 'w', encoding='utf-8') as f:
            f.write(text + '\n')
    print(text)
    shutil.rmtree(tmp, ignore_errors=True)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
