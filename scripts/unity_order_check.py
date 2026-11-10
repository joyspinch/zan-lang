#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Unity composite-include gate：拼接序与 UNITY-ORDER 标注的机检（WC §10）。

编译器与运行时用 composite dispatcher（顶层 .c #include 子目录 .c）组织超大
TU，部分拼接顺序承载语义（rt_io_co_pool.c 必须先于 rt_io_co_sched.c 拼入，
为 POSIX 提供 pthread→Win32 shim）。自然语言注释已经腐烂过两次：
irgen_call_async.c 与 reflect/reflect_thunks.c 描述的"unity 排序"在两者改为
独立编译 TU 后失效。本门禁用机器可读标注固化拼接约束：

    UNITY-ORDER: before <unit.c>   本单元拼入序必须在目标之前
    UNITY-ORDER: after  <unit.c>   本单元拼入序必须在目标之后
    UNITY-ORDER: needs  <unit.c>   必须与目标同 TU，顺序不限

检查项：
  1. 标注引用的目标 .c 必须真实存在；
  2. 标注文件与目标必须在至少一个真实拼接 TU 中共现，且相对顺序成立；
  3. 同一单元不得被同一 TU 重复拼入（直接或嵌套）；
  4. 同一 TU 内同守卫组的单元之间，文件级 static 名不得冲突——编译器只能在
     实际编译该分支的平台上发现这类冲突，Windows 开发机永远编不到 Linux 分支；
  5. 函数体拼接片段（FRAGMENT: host=<fn>）契约机检：
     - 片段必须被 #include 于宿主函数的函数体内；
     - consumes 清单变量必须在宿主函数该包含点前已在作用域中，且在片段内被引用；
     - defines 清单变量必须在片段内声明/赋值，且在宿主后续片段或函数体延续部分被消费。

用法：python3 scripts/unity_order_check.py [--root REPO_ROOT]
退出码：0 干净；1 有违规（逐条打到 stdout）。
"""

import argparse
import re
import sys
from pathlib import Path, PurePosixPath

SCAN_ROOTS = ("src/compiler", "src/runtime")

MARKER_RE = re.compile(r"UNITY-ORDER:\s*(before|after|needs)\s+([\w./\\-]+\.c)")
INCLUDE_C_RE = re.compile(r'^\s*#include\s*"([^"]+\.c)"')
COND_IF_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b(.*)")
COND_ELIF_RE = re.compile(r"^\s*#\s*elif\b")
COND_ELSE_RE = re.compile(r"^\s*#\s*else\b")
COND_ENDIF_RE = re.compile(r"^\s*#\s*endif\b")

# 文件级 static 定义名的启发式提取：仅取第 0 列起写的 static 行（函数体内
# 的局部 static 有缩进，不进 TU 命名空间），取第一个 ( = [ 前的最后一个
# 标识符；类型关键字不是名字（防多行声明把 "void" 当名字）。
TYPE_KEYWORDS = frozenset((
    "void", "char", "short", "int", "long", "float", "double", "signed",
    "unsigned", "bool", "size_t", "struct", "union", "enum", "const",
    "inline", "static", "volatile", "restrict", "LLVMTypeRef", "LLVMValueRef",
))
STATIC_NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
STATIC_CUT_RE = re.compile(r"[=(\[]")

FRAGMENT_RE = re.compile(
    r"/\*.*?\bFRAGMENT:\s*host=([A-Za-z0-9_]+)(?:\(\))?\b(.*?)\*/", re.DOTALL
)


def parse_fragment_contract(content: str):
    m = FRAGMENT_RE.search(content[:2000])
    if not m:
        return None
    host_fn = m.group(1)
    body = m.group(2)
    lines = [re.sub(r"^\s*\*\s*", "", l) for l in body.splitlines()]
    cleaned = "\n".join(lines)

    m_cons = re.search(r"consumes:\s*(.*?)(?=\bdefines:|[（(]|$)", cleaned, re.DOTALL)
    m_defs = re.search(r"defines:\s*(.*?)(?=\bconsumes:|[（(]|$)", cleaned, re.DOTALL)

    consumes = []
    if m_cons:
        consumes = [w for w in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", m_cons.group(1)) if w != "none"]
    defines = []
    if m_defs:
        defines = [w for w in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", m_defs.group(1)) if w != "none"]

    return host_fn, consumes, defines


def find_function_body(content: str, fn_name: str):
    pat = re.compile(r"\b" + fn_name + r"\s*\([^)]*\)\s*\{", re.MULTILINE)
    m = pat.search(content)
    if not m:
        return None
    start = m.start()
    brace_start = m.end() - 1
    depth = 1
    i = brace_start + 1
    in_str = False
    in_line_comment = False
    in_block_comment = False
    while i < len(content):
        c = content[i]
        if in_line_comment:
            if c == "\n":
                in_line_comment = False
        elif in_block_comment:
            if c == "*" and i + 1 < len(content) and content[i + 1] == "/":
                in_block_comment = False
                i += 1
        elif in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
        else:
            if c == "/" and i + 1 < len(content):
                if content[i + 1] == "/":
                    in_line_comment = True
                    i += 1
                elif content[i + 1] == "*":
                    in_block_comment = True
                    i += 1
            elif c == '"':
                in_str = True
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return start, brace_start, i + 1
        i += 1
    return start, brace_start, len(content)


def get_fragment_full_content(path: Path, resolve_fn, visited=None) -> str:
    if visited is None:
        visited = set()
    if path in visited or not path.is_file():
        return ""
    visited.add(path)
    try:
        content = path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return ""
    out = [content]
    for m in re.finditer(r'^\s*#include\s*"([^"]+\.c)"', content, re.MULTILINE):
        sub_path = resolve_fn(path, m.group(1))
        if sub_path and sub_path.is_file():
            out.append(get_fragment_full_content(sub_path, resolve_fn, visited))
    return "\n".join(out)


def norm_rel(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def scan_file(path: Path):
    """Return (includes, markers, statics) parsed from one .c file.

    includes: list of (lineno, raw_target, branch_path)  # 指令所在条件分支
    markers:  list of (lineno, kind, raw_target)
    statics:  list of (name, lineno, branch_path)        # 文件内条件分支
    branch_path: ((cond_lineno, alt), ...)  alt: 0=if, 1..n=elif, -1=else。
    两符号冲突当且仅当同名且分支路径相同——不同分支（win/posix、
    iocp/select）互斥拼入，同名是平台分派不是重定义。
    """
    includes, markers, statics = [], [], []
    stack = []  # [(cond_lineno, alt_index)]
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for lineno, line in enumerate(fh, 1):
            m = COND_IF_RE.match(line)
            if m:
                stack.append((lineno, 0))
                continue
            if COND_ENDIF_RE.match(line):
                if stack:
                    stack.pop()
                continue
            if COND_ELIF_RE.match(line):
                if stack:
                    stack[-1] = (lineno, stack[-1][1] + 1)
                continue
            if COND_ELSE_RE.match(line):
                if stack:
                    stack[-1] = (lineno, -1)
                continue
            bpath = tuple(stack)

            m = INCLUDE_C_RE.match(line)
            if m:
                includes.append((lineno, m.group(1), bpath))
                continue
            m = MARKER_RE.search(line)
            if m:
                markers.append((lineno, m.group(1), m.group(2)))
                continue
            if line.startswith("static"):
                # 只比对"定义"：函数定义的 { 在本行，或变量定义带 =。纯原型
                # （…); 结尾、无 { 无 =）跳过——本库惯例是早单元前向声明、
                # 晚单元定义，原型跨单元重复是合法的。
                if "{" not in line and "=" not in line:
                    continue
                cut = STATIC_CUT_RE.split(line, 1)[0]
                names = STATIC_NAME_RE.findall(cut)
                name = None
                for cand in reversed(names):
                    if cand not in TYPE_KEYWORDS:
                        name = cand
                        break
                if name:
                    statics.append((name, lineno, bpath))
    return includes, markers, statics


class Checker:
    def __init__(self, root: Path):
        self.root = root
        self.files = {}      # abs Path -> scan result
        self.rel_of = {}     # abs Path -> display rel string
        self.tus = []        # list of dicts: top, units, index_by_rel
        self.fragments = {}  # abs Path -> (host_fn, consumes, defines)
        self.violations = []

    def load_all(self):
        for scan_root in SCAN_ROOTS:
            base = self.root / scan_root
            if not base.is_dir():
                continue
            for path in sorted(base.rglob("*.c")):
                self.files[path] = scan_file(path)
                self.rel_of[path] = norm_rel(path, self.root)
                try:
                    txt = path.read_text(encoding="utf-8", errors="replace")
                except Exception:
                    continue
                contract = parse_fragment_contract(txt)
                if contract:
                    self.fragments[path] = contract

    def resolve(self, includer: Path, raw: str):
        """Resolve an #include target: includer-relative, then scan-root, then repo-root."""
        cand = (includer.parent / raw)
        if cand.is_file():
            return cand
        for scan_root in SCAN_ROOTS:
            cand = self.root / scan_root / raw
            if cand.is_file():
                return cand
        cand = self.root / raw
        if cand.is_file():
            return cand
        return None

    def build_tus(self):
        for path, (includes, _markers, _statics) in sorted(self.files.items()):
            if not includes:
                continue
            units = []          # (abs_path, include_site_branch_path)
            index_by_rel = {}   # rel -> first index
            missing, dup = [], []
            self._walk_top(path, units, index_by_rel, missing, dup)
            self.tus.append({
                "top": self.rel_of[path],
                "units": units,
                "index_by_rel": index_by_rel,
            })
            for rel, lineno, raw in missing:
                self.violations.append(
                    f"{self.rel_of[path]}:{lineno}: include target not found: {raw}")
            for rel, first_ln in dup:
                self.violations.append(
                    f"{self.rel_of[path]}: unit {rel} spliced twice in the same "
                    f"conditional context (first splice order {first_ln}); "
                    f"duplicate splice is a guaranteed redefinition")

    def _walk_top(self, top, units, index_by_rel, missing, dup):
        # DFS 前序：单元次序 = 拼接后的文本序，嵌套 include 保持深度优先。
        # visited 以 (rel, include-site 分支路径) 为键：同单元在不同互斥分支
        # 各拼一次是合法的平台分派；同分支拼两次才是重定义。
        visited = set()

        def walk(path, inc_bpath):
            rel = self.rel_of[path]
            site = (rel, inc_bpath)
            first = site not in visited
            index = len(units)
            units.append((path, inc_bpath))
            index_by_rel.setdefault(rel, index)
            if not first:
                dup.append((rel, index_by_rel[rel]))
                return  # 该分支上下文已展开过，不再递归
            visited.add(site)
            for _lineno, raw, site_bpath in self.files[path][0]:
                target = self.resolve(path, raw)
                if target is None or target not in self.files:
                    missing.append((rel, _lineno, raw))
                    continue
                walk(target, site_bpath)

        walk(top, ())

    def check_markers(self):
        for path, (_inc, markers, _statics) in sorted(self.files.items()):
            if not markers:
                continue
            f_rel = self.rel_of[path]
            for lineno, kind, raw in markers:
                target = self.resolve(path, raw)
                if target is None or target not in self.files:
                    self.violations.append(
                        f"{f_rel}:{lineno}: UNITY-ORDER target not found: {raw}")
                    continue
                t_rel = self.rel_of[target]
                # 标注须在至少一个真实 TU 中共现；顺序约束在每个共现 TU 中成立。
                if kind == "needs":
                    ok_tus = [tu for tu in self.tus
                              if f_rel in tu["index_by_rel"] and t_rel in tu["index_by_rel"]]
                    if not ok_tus:
                        self.violations.append(
                            f"{f_rel}:{lineno}: UNITY-ORDER needs {t_rel}: no composite "
                            f"TU contains both units")
                    continue
                cmp_lt = kind == "before"
                ok_tus, bad_tus = [], []
                for tu in self.tus:
                    idx_f = tu["index_by_rel"].get(f_rel)
                    idx_t = tu["index_by_rel"].get(t_rel)
                    if idx_f is None or idx_t is None:
                        continue
                    (ok_tus if (idx_f < idx_t) == cmp_lt else bad_tus).append(tu["top"])
                if not ok_tus and not bad_tus:
                    self.violations.append(
                        f"{f_rel}:{lineno}: UNITY-ORDER {kind} {t_rel}: no composite "
                        f"TU contains both units")
                elif bad_tus:
                    self.violations.append(
                        f"{f_rel}:{lineno}: UNITY-ORDER {kind} {t_rel} violated in "
                        f"TU(s): {', '.join(bad_tus)}")

    def check_static_collisions(self):
        for tu in self.tus:
            seen = {}
            for path, inc_bpath in tu["units"]:
                rel = self.rel_of[path]
                for name, lineno, own_bpath in self.files[path][2]:
                    # 有效分支路径 = 拼接站点分支 + 单元内分支：不同条件上下文
                    # 互斥编译，同名 static 是平台分派不是冲突。
                    key = (name, inc_bpath + own_bpath)
                    if key in seen:
                        other_rel, other_ln = seen[key]
                        if other_rel != rel:
                            self.violations.append(
                                f"{rel}:{lineno}: file-scope static '{name}' already "
                                f"defined at {other_rel}:{other_ln} in the same TU "
                                f"'{tu['top']}' (same conditional context)")
                    else:
                        seen[key] = (rel, lineno)

    def check_fragment_contracts(self):
        for frag_path, (host_fn, consumes, defines) in sorted(self.fragments.items()):
            frag_rel = self.rel_of[frag_path]
            includers = []
            for file_path, (includes, _, _) in self.files.items():
                for lineno, raw, _ in includes:
                    if self.resolve(file_path, raw) == frag_path:
                        includers.append((file_path, lineno))
            if not includers:
                self.violations.append(
                    f"{frag_rel}: FRAGMENT declared host={host_fn}, "
                    f"but file is never included in any compilation unit"
                )
                continue

            for host_file, inc_lineno in includers:
                host_rel = self.rel_of[host_file]
                try:
                    host_content = host_file.read_text(encoding="utf-8", errors="replace")
                except Exception:
                    continue
                fn_info = find_function_body(host_content, host_fn)
                if not fn_info:
                    self.violations.append(
                        f"{frag_rel}: FRAGMENT declared host={host_fn}, "
                        f"but host function '{host_fn}()' was not found in includer {host_rel}"
                    )
                    continue

                fn_start, brace_start, fn_end = fn_info
                fn_body = host_content[brace_start:fn_end]
                inc_matches = list(re.finditer(r'^\s*#include\s*"([^"]+\.c)"', fn_body, re.MULTILINE))

                target_inc = None
                for inc in inc_matches:
                    if self.resolve(host_file, inc.group(1)) == frag_path:
                        target_inc = inc
                        break

                if not target_inc:
                    self.violations.append(
                        f"{frag_rel}: FRAGMENT declared host={host_fn}, "
                        f"but is included outside {host_fn}() body in {host_rel}:{inc_lineno}"
                    )
                    continue

                fn_scope = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", host_content[:brace_start]))
                last_pos = 0
                frag_tokens = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*",
                                             get_fragment_full_content(frag_path, self.resolve)))

                for idx, inc in enumerate(inc_matches):
                    resolved = self.resolve(host_file, inc.group(1))
                    chunk = fn_body[last_pos:inc.start()]
                    fn_scope.update(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", chunk))
                    last_pos = inc.end()

                    if resolved == frag_path:
                        for c in consumes:
                            if c not in fn_scope:
                                self.violations.append(
                                    f"{frag_rel}: consumes '{c}', but it is not in scope "
                                    f"prior to inclusion in {host_fn}() of {host_rel}"
                                )
                            if c not in frag_tokens:
                                self.violations.append(
                                    f"{frag_rel}: consumes '{c}', but it is never referenced "
                                    f"in fragment"
                                )

                        downstream_tokens = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", fn_body[inc.end():]))
                        for sub_idx in range(idx + 1, len(inc_matches)):
                            sub_res = self.resolve(host_file, inc_matches[sub_idx].group(1))
                            if sub_res:
                                downstream_tokens.update(
                                    re.findall(r"[A-Za-z_][A-Za-z0-9_]*",
                                               get_fragment_full_content(sub_res, self.resolve))
                                )

                        for d in defines:
                            if d not in frag_tokens:
                                self.violations.append(
                                    f"{frag_rel}: defines '{d}', but it is never declared or assigned "
                                    f"in fragment"
                                )
                            if d not in downstream_tokens:
                                self.violations.append(
                                    f"{frag_rel}: defines '{d}', but it is never consumed downstream "
                                    f"in {host_fn}() continuation or subsequent fragments"
                                )

                    if resolved in self.fragments:
                        _, _, res_defs = self.fragments[resolved]
                        fn_scope.update(res_defs)

    def run(self):
        self.load_all()
        self.build_tus()
        self.check_markers()
        self.check_static_collisions()
        self.check_fragment_contracts()
        return self.violations


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=None, help="repo root (default: scripts/..)")
    args = ap.parse_args()
    root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parent.parent

    checker = Checker(root)
    violations = checker.run()

    n_units = sum(len(tu["units"]) for tu in checker.tus)
    n_markers = sum(len(info[1]) for info in checker.files.values())
    n_frags = len(checker.fragments)
    if violations:
        for v in violations:
            print(v)
        print(f"unity_order_check: {len(violations)} violation(s) across "
              f"{len(checker.tus)} composite TU(s) / {n_units} spliced units / "
              f"{n_markers} marker(s) / {n_frags} fragment contract(s)")
        return 1
    print(f"unity_order_check: OK - {len(checker.tus)} composite TU(s), "
          f"{n_units} spliced units, {n_markers} marker(s), "
          f"{n_frags} fragment contract(s) verified, static namespaces clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
