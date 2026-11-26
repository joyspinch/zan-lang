"""S3（docs/ZAN_STANDARD.md §1）：zanc --emit-deps + scripts/affected.py 端到端。

用真 zanc 以 --emit-deps 编夹具，再在工作目录里造一份 CTestTestfile.cmake
（三条用例：带依赖记录的 case_a、它的 det 孪生、无关的 case_b），逐项核对：
stdlib 依赖命中、源文件命中与 det 孪生跳过/--with-det、原生源码只报不猜、
超预算不给命令。"""
import argparse
import json
import os
import subprocess
import sys


def affected(root, build, *args):
    r = subprocess.run([sys.executable, '-I', '-B', os.path.join(root, 'scripts', 'affected.py'),
                        '--build-dir', build, '--json', *args],
                       cwd=root, capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr
    return json.loads(r.stdout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zanc', required=True)
    ap.add_argument('--root', required=True)
    ap.add_argument('--work', required=True)
    a = ap.parse_args()
    root = os.path.realpath(a.root)
    work = os.path.realpath(a.work)
    os.makedirs(work, exist_ok=True)
    src = os.path.join(root, 'tests', 'affected', 'affected_fixture.zan')
    other = os.path.join(root, 'tests', 'summary', 'summary_surface.zan')
    exe = os.path.join(work, 'case_a')
    r = subprocess.run([a.zanc, src, '--auto-stdlib', '-o', exe, '--emit-deps', exe + '.deps'],
                       cwd=root, capture_output=True, text=True, timeout=300)
    assert r.returncode == 0, r.stdout + r.stderr
    deps = [l.strip() for l in open(exe + '.deps', encoding='utf-8') if l.strip()]
    enc = os.path.realpath(os.path.join(root, 'stdlib', 'System', 'Text', 'Encoding.zan'))
    assert os.path.realpath(src) in deps, deps
    assert enc in deps, 'Encoding.zan missing from --emit-deps output'
    assert all(os.path.isabs(d) for d in deps), deps

    runner = os.path.join(root, 'tests', 'run_case.cmake')
    with open(os.path.join(work, 'CTestTestfile.cmake'), 'w', encoding='utf-8') as f:
        f.write('add_test(case_a "cmake" "-DSRC=%s" "-DOUT_EXE=%s" "-P" "%s")\n' % (src, exe, runner))
        f.write('add_test(case_det_a "cmake" "-DSRC=%s" "-DOUT_EXE=%s" "-P" "%s")\n'
                % (src, os.path.join(work, 'case_det_a'), runner))
        f.write('add_test(case_b "cmake" "-DSRC=%s" "-P" "%s")\n' % (other, runner))

    got = affected(root, work, 'stdlib/System/Text/Encoding.zan')
    assert got['tests'] == ['case_a'], got

    got = affected(root, work, os.path.relpath(src, root))
    assert got['tests'] == ['case_a'] and got['det_skipped'] == 1, got
    got = affected(root, work, '--with-det', os.path.relpath(src, root))
    assert got['tests'] == ['case_a', 'case_det_a'], got

    got = affected(root, work, 'src/compiler/checker.c')
    assert got['tests'] == [] and any('native' in n for n in got['notes']), got

    got = affected(root, work, '--budget', '0', os.path.relpath(src, root))
    assert got['over_budget'] and got['ctest_regex'] is None, got

    got = affected(root, work, 'tests/run_case.cmake')
    assert sorted(got['tests']) == ['case_a', 'case_b'], got
    print('affected: emit-deps and selection contracts passed')


if __name__ == '__main__':
    main()
