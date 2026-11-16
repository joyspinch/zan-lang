"""S-UNWIND pass red/green: nounwind closure + invoke->call collapse.

Usage: python -I test_shrink_unwind.py --zanc <path> [--work <dir>]

Red   (ZAN_NO_UNWIND_SHRINK=1): invokes stay, stderr line absent.
Green (default):               nothrow invokes collapsed, raise path intact,
                               stdout byte-identical, .text not larger.
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
    src = Path(__file__).resolve().parent / 'fixtures' / 'shrink_unwind.zan'

    summary = {}
    for tag, env_off in (('red', True), ('green', False)):
        env = os.environ.copy()
        env['ZAN_NO_OBF'] = '1'
        env['ZAN_DUMP_OPT_IR'] = str(work / f'shrink_{tag}.ll')
        if env_off:
            env['ZAN_NO_UNWIND_SHRINK'] = '1'
        exe = work / f'shrink_{tag}.exe'
        r = run([str(zanc), str(src), '--auto-stdlib', '--publish', '-o',
                 str(exe)], env)
        stderr = r.stderr.decode('utf-8', errors='replace')
        assert r.returncode == 0, (tag, r.returncode, stderr[-800:])
        m = re.search(r'shrink: nounwind: (\d+)/(\d+) invokes collapsed', stderr)
        summary[tag] = {
            'stderr_line': m.group(0) if m else None,
            'collapsed': int(m.group(1)) if m else 0,
            'total_invokes': int(m.group(2)) if m else 0,
            'exe_bytes': exe.stat().st_size,
            'ir_invokes': 0,
            'ir_raise_intact': False,
        }
        ir = (work / f'shrink_{tag}.ll').read_text(encoding='utf-8',
                                                    errors='replace')
        summary[tag]['ir_invokes'] = len(
            re.findall(r'^\s*invoke\b', ir, re.M))
        summary[tag]['ir_raise_intact'] = (
            'invoke void @RaiseException' in ir or
            'call void @RaiseException' in ir)
        runexe = run([str(exe)], env)
        out = runexe.stdout.replace(b'\r\n', b'\n')
        assert runexe.returncode == 0, (tag, runexe.returncode)
        summary[tag]['stdout'] = out.decode('utf-8', errors='replace')

    # 绿：塌缩真实发生且写 stderr 行；红：无该行
    assert summary['green']['collapsed'] > 0, summary['green']
    assert summary['red']['stderr_line'] is None, summary['red']
    # 绿 IR 中剩余 invoke ≤ 总数 - 塌缩数（后续 LLVM 管线对死 lpad
    # 还会继续 invoke→call 展平，只许更多不许更少）
    g = summary['green']
    assert g['ir_invokes'] <= g['total_invokes'] - g['collapsed'], g
    # raise 站点两个 build 都在（EH 语义未动）
    assert summary['red']['ir_raise_intact'], summary['red']
    assert g['ir_raise_intact'], g
    # 行为逐字节一致
    assert summary['red']['stdout'] == g['stdout'] != '', g
    # 尺寸不增（塌缩是纯收益）
    assert g['exe_bytes'] <= summary['red']['exe_bytes'], summary
    assert g['ir_invokes'] < summary['red']['ir_invokes'], summary

    print('shrink unwind red/green: PASS')
    print('  red  : invokes_ir=%d exe=%d stdout=%r'
          % (summary['red']['ir_invokes'], summary['red']['exe_bytes'],
             summary['red']['stdout']))
    print('  green: invokes_ir=%d exe=%d collapsed=%d/%d stdout=%r'
          % (g['ir_invokes'], g['exe_bytes'], g['collapsed'],
             g['total_invokes'], g['stdout']))


if __name__ == '__main__':
    main()
