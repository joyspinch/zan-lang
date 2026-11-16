"""S-UNWIND 跨片抛出回归钉（B 刀收口硬性面，design.md §11）。

Usage: python -I test_shard_cross_throw_unwind.py --zanc <path> [--work <dir>]

ZAN_SHARD=1 + ZAN_SHARD_MAX_FN=1 把 thrower 与 catch caller 强制拆进
不同分片：caller 模块里 thrower 只是 declaration（Zan 名注册表命中，
恒 MAYTHROW）。塌缩若误拆该 invoke，异常逃逸到顶层，本测试必红。

绿（默认）：catch 正常、stdout 与红逐字节一致、无顶层异常。
红（ZAN_NO_UNWIND_SHRINK=1）：行为基线。
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


def run(cmd, env):
    return subprocess.run(cmd, env=env, capture_output=True, timeout=180)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zanc', type=Path, required=True)
    ap.add_argument('--work', type=Path, default=Path('build/test-out'))
    args = ap.parse_args()
    zanc = args.zanc.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    src = (Path(__file__).resolve().parent / 'fixtures' /
           'shard_cross_throw_unwind.zan')

    summary = {}
    for tag, off in (('red', True), ('green', False)):
        env = os.environ.copy()
        env['ZAN_SHARD'] = '1'
        env['ZAN_SHARD_MAX_FN'] = '1'
        if off:
            env['ZAN_NO_UNWIND_SHRINK'] = '1'
        exe = work / f'xthrow_{tag}.exe'
        r = run([str(zanc), str(src), '--auto-stdlib', '--publish', '-o',
                 str(exe)], env)
        stderr = r.stderr.decode('utf-8', errors='replace')
        assert r.returncode == 0, (tag, r.returncode, stderr[-800:])
        # 布局守卫：必须真的分了 ≥2 片，否则本钉没有钉住目标形态
        m = re.search(r'planning \d+ eligible.*?(\d+) objects', stderr)
        assert m and int(m.group(1)) >= 2, (tag, stderr[-500:])
        runexe = run([str(exe)], env)
        out = runexe.stdout.replace(b'\r\n', b'\n')
        assert runexe.returncode == 0, (tag, runexe.returncode,
                                        runexe.stderr.decode('utf-8',
                                                             'replace')[-500:])
        summary[tag] = {'stdout': out, 'stderr': stderr,
                        'm': re.search(r'shrink: nounwind: (\d+)/(\d+)',
                                       stderr)}

    # 绿：本夹具所有被调者都是跨片 Zan 声明（注册表命中 → 恒 MAYTHROW），
    # 正确实现必然 0 塌缩——塌缩数 >0 反而是误判信号
    assert summary['green']['m'], summary['green']['stderr'][-300:]
    assert int(summary['green']['m'].group(1)) == 0, summary['green']
    # 红绿行为逐字节一致；catch 路径正常（7 + done）；无顶层异常逃逸
    assert summary['red']['stdout'] == summary['green']['stdout'], summary
    assert summary['green']['stdout'] == b'7\ndone\n', summary['green']
    assert 'Unhandled exception' not in summary['green']['stderr'], summary

    print('shard cross throw unwind: PASS')
    print('  collapsed=%s/%s stdout=%r'
          % (summary['green']['m'].group(1), summary['green']['m'].group(2),
             summary['green']['stdout']))


if __name__ == '__main__':
    main()
