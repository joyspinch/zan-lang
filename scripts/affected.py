#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""S3（docs/ZAN_STANDARD.md §1）：改动文件 → 受影响的 ctest 用例名单（WC §8 定向命中面）。

替代人工挑选：每条用例引用的仓库文件来自两处——
  1. 用例自身的注册参数（build/**/CTestTestfile.cmake 的 add_test：-DSRC、
     ZANC_ARGS 里的源文件/夹具、-DEXPECTED 金样、-P 驱动脚本……）；
  2. 编译器记下的真实依赖：测试驱动以 `zanc --emit-deps <产物>.deps` 编译，
     列出拉入过滤之后实际编进程序的每个源文件（标准库、包都在内）。
改动文件落在某用例的引用集合里，该用例即受影响。

不硬猜的情形如实报告（notes），不混进名单：
  - 编译器/运行时/驱动等原生源码（.c/.h/.cpp/.m）：影响所有程序，没有可靠的
    定向面——按改动的功能自己点名用例，或走档位（用户点名/发布门）；
  - CMakeLists.txt / cmake/：用例注册本身变了，先重新 configure；
  - 文档、任务台账、技能：没有用例。
从未带 --emit-deps 编译过的用例只按注册参数匹配，计数报在 notes 里。

默认避开 det 孪生（case_det_*，WC §8）；名单超过预算（默认 80）只报数量不给
命令。

用法：
  python3 scripts/affected.py stdlib/System/Text/Encoding.zan ...
  python3 scripts/affected.py --git            # 工作区相对 HEAD 的改动 + 未跟踪文件
  python3 scripts/affected.py --diff x.patch   # 统一 diff 里的文件
  选项：--build-dir build  --budget 80  --with-det  --json
退出码：0 正常（含超预算，见输出）；2 参数/环境问题。
"""
import argparse
import json
import os
import re
import subprocess
import sys

NATIVE_EXT = ('.c', '.h', '.cpp', '.hpp', '.cc', '.m', '.mm', '.inc')
DOC_PREFIXES = ('docs/', '.agents/', 'tools/ai_pack/', '.claude/')
DOC_FILES = ('TASKS.md', 'AGENTS.md', 'README.md', 'CLAUDE.md')

ARG_RE = re.compile(r'\[=\[(.*?)\]=\]|"((?:[^"\\]|\\.)*)"|(\S+)', re.S)


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def norm(path, root):
    p = os.path.realpath(path if os.path.isabs(path) else os.path.join(root, path))
    return p


def split_args(text):
    out = []
    for m in ARG_RE.finditer(text):
        if m.group(1) is not None:
            out.append(m.group(1))
        elif m.group(2) is not None:
            out.append(m.group(2).replace('\\"', '"').replace('\\\\', '\\'))
        else:
            out.append(m.group(3))
    return out


def load_tests(build_dir):
    """{name: {"refs": set(abs paths), "out_exe": path|None}}"""
    tests = {}
    for dirpath, _dirs, files in os.walk(build_dir):
        if 'CTestTestfile.cmake' not in files:
            continue
        with open(os.path.join(dirpath, 'CTestTestfile.cmake'), encoding='utf-8',
                  errors='replace') as f:
            for line in f:
                if not line.startswith('add_test('):
                    continue
                body = line[len('add_test('):].rstrip().rstrip(')')
                args = split_args(body)
                if not args:
                    continue
                name, rest = args[0], args[1:]
                refs, out_exe = set(), None
                for a in rest:
                    val = a
                    if a.startswith('-D') and '=' in a:
                        key, val = a[2:].split('=', 1)
                        if key == 'OUT_EXE':
                            out_exe = val
                            continue
                    for item in val.split(';'):
                        if item and (item.startswith('/') or re.match(r'^[A-Za-z]:[\\/]', item)):
                            refs.add(os.path.realpath(item))
                tests[name] = {'refs': refs, 'out_exe': out_exe}
    return tests


def load_deps(out_exe):
    for cand in (out_exe + '.deps',):
        if os.path.isfile(cand):
            with open(cand, encoding='utf-8', errors='replace') as f:
                return {os.path.realpath(l.strip()) for l in f if l.strip()}
    return None


def git_changed(root):
    def run(*cmd):
        r = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit('affected.py: ' + ' '.join(cmd) + ' failed: ' + r.stderr.strip())
        return [l for l in r.stdout.splitlines() if l.strip()]
    files = run('git', 'diff', '--name-only', 'HEAD')
    files += run('git', 'ls-files', '--others', '--exclude-standard')
    return sorted(set(files))


def diff_files(path):
    files = set()
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            for prefix in ('+++ b/', '--- a/'):
                if line.startswith(prefix):
                    files.add(line[len(prefix):].strip())
    return sorted(files)


def classify(rel):
    relp = rel.replace('\\', '/')
    if relp in ('CMakeLists.txt',) or relp.startswith('cmake/'):
        return 'registration'
    if relp.startswith(DOC_PREFIXES) or relp in DOC_FILES:
        return 'docs'
    if relp.startswith('src/') and relp.endswith(NATIVE_EXT):
        return 'native'
    return 'mapped'


def main():
    ap = argparse.ArgumentParser(description='changed files -> affected ctest cases (S3)')
    ap.add_argument('paths', nargs='*')
    ap.add_argument('--git', action='store_true')
    ap.add_argument('--diff')
    ap.add_argument('--build-dir', default='build')
    ap.add_argument('--budget', type=int, default=80)
    ap.add_argument('--with-det', action='store_true')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()

    root = repo_root()
    changed = list(a.paths)
    if a.git:
        changed += git_changed(root)
    if a.diff:
        changed += diff_files(a.diff)
    if not changed:
        ap.error('no changed files (give paths, --git or --diff)')
    build_dir = a.build_dir if os.path.isabs(a.build_dir) else os.path.join(root, a.build_dir)
    if not os.path.isfile(os.path.join(build_dir, 'CTestTestfile.cmake')):
        sys.exit('affected.py: no CTestTestfile.cmake under %s (configure first)' % build_dir)

    tests = load_tests(build_dir)
    notes, mapped = [], []
    for rel in changed:
        kind = classify(os.path.relpath(norm(rel, root), root))
        if kind == 'native':
            notes.append('%s: native compiler/runtime source -- affects every program; '
                         'name the cases for the feature you changed, or run a tier only '
                         'when asked (WC §8)' % rel)
        elif kind == 'registration':
            notes.append('%s: test registration changed -- re-run cmake, then pick the '
                         'new/changed cases by name' % rel)
        elif kind == 'docs':
            notes.append('%s: documentation -- no test cases' % rel)
        mapped.append(norm(rel, root))  # 原生文件若被用例直接引用（夹具），照样命中

    hits, no_deps, det_skipped = [], 0, 0
    targets = set(mapped)
    for name, t in sorted(tests.items()):
        refs = t['refs']
        if t['out_exe']:
            deps = load_deps(t['out_exe'])
            if deps is None:
                no_deps += 1
            else:
                refs = refs | deps
        if refs & targets:
            if name.startswith('case_det_') and not a.with_det:
                det_skipped += 1
                continue
            hits.append(name)
    if no_deps:
        notes.append('%d test(s) have no dependency record yet (never compiled with '
                     '--emit-deps): matched by their registered sources only' % no_deps)

    over = len(hits) > a.budget
    regex = '^(' + '|'.join(re.escape(h) for h in hits) + ')$' if hits else ''
    if a.json:
        print(json.dumps({'tests': hits, 'count': len(hits), 'budget': a.budget,
                          'over_budget': over, 'det_skipped': det_skipped,
                          'ctest_regex': None if over or not hits else regex,
                          'notes': notes}, ensure_ascii=False, indent=2))
        return 0
    print('affected: %d test(s)%s' % (len(hits), (' (det twins skipped: %d)' % det_skipped)
                                       if det_skipped else ''))
    for h in hits[:a.budget if over else len(hits)]:
        print('  ' + h)
    if over:
        print('over budget (%d > %d): narrow the change set or name the cases' %
              (len(hits), a.budget))
    elif hits:
        print('run: cd %s && ctest -R "%s"' % (os.path.relpath(build_dir, root), regex))
    if notes:
        print('notes:')
        for n in notes:
            print('  ' + n)
    return 0


if __name__ == '__main__':
    sys.exit(main())
