#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""packages/ 依赖门禁：包间不得成环，基础层不得向上依赖（WC §14/§17）。

zan.pkg 不声明依赖，包间依赖全靠源码里的 `using` 隐式成立，所以 Zan.Text ↔
Zan.Json 这类环能长期潜伏（2026-11 修于 848fa8d9f）。本门禁从源码重建包依赖
图并检查：

  1. 包依赖图无环（强连通分量大小必须为 1）；
  2. 基础层（FOUNDATION）只能依赖基础层——它们被几乎所有包依赖，向上依赖
     会把上层整包拖进每个程序的编译图。

依赖解析口径（避免共享命名空间造成假边）：
  - 命名空间的"拥有者"= 声明它的包；目录路径派生的命名空间同样计入（包索引
    按目录派生命名空间找源文件，`using Sdk.Wechat.Models.TenPay` 即属此类）；
    stdlib 也算一个拥有者。
  - `using N` 只有一个拥有者且是别的包时，直接成边；
  - N 有多个拥有者（如 Gui.Component 跨 6 个包、System.Text 同时在 stdlib 与
    Zan.Text）时，只有当该文件（去掉注释与字符串后）真正引用了某拥有包在 N
    下声明的类型名，才成边。
  - 包内 `tests/` 目录不计（测试可依赖 Zan.Testing 等）。

用法：python3 scripts/package_deps_check.py [--root REPO] [--graph]
退出码：0 干净；1 有违规（逐条打到 stdout）。
"""

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

# 基础层：只能依赖本集合内的包。新增基础包或放宽时改这里并写明理由。
FOUNDATION = {
    "Zan.IO",
    "Zan.Text",
    "Zan.Json",
    "Zan.Security",
    "Zan.Diagnostics",
    "Zan.Linq",
    "Zan.Testing",
}

NS_RE = re.compile(r"^\s*namespace\s+([\w.]+)\s*;", re.M)
USING_RE = re.compile(r"^\s*using\s+([\w.]+)\s*;", re.M)
TYPE_RE = re.compile(
    r"^\s*(?:(?:public|internal|private|protected|static|abstract|sealed|partial)\s+)*"
    r"(?:class|struct|interface|enum|delegate\s+[\w<>\[\],. ]+?)\s+([A-Za-z_]\w*)",
    re.M,
)
STRIP_RE = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])\'', re.S
)
IDENT_RE = re.compile(r"[A-Za-z_]\w*")
STDLIB = "<stdlib>"


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def dir_namespace(path, src_root):
    rel = path.parent.relative_to(src_root)
    return ".".join(rel.parts)


def scan_owner(owner, src_roots, owners, types):
    """登记 owner 拥有的命名空间及其下声明的类型名。"""
    for src_root in src_roots:
        for f in src_root.rglob("*.zan"):
            text = read(f)
            m = NS_RE.search(text)
            declared = m.group(1) if m else ""
            dns = dir_namespace(f, src_root)
            for ns in {declared, dns} - {""}:
                owners[ns].add(owner)
            if declared:
                for t in TYPE_RE.finditer(text):
                    types[(declared, owner)].add(t.group(1))


def build_graph(root):
    pkg_root = root / "packages"
    owners = defaultdict(set)
    types = defaultdict(set)
    scan_owner(STDLIB, [root / "stdlib"], owners, types)
    packages = sorted(p.name for p in pkg_root.iterdir() if (p / "zan.pkg").is_file())
    for p in packages:
        src = pkg_root / p / "src"
        if src.is_dir():
            scan_owner(p, [src], owners, types)

    deps = defaultdict(set)
    evidence = {}
    for p in packages:
        src = pkg_root / p / "src"
        if not src.is_dir():
            continue
        for f in src.rglob("*.zan"):
            text = read(f)
            idents = None
            used = [u.group(1) for u in USING_RE.finditer(text)]
            # 本文件所在命名空间无需 using 即可见：共享命名空间（Gui.Component）
            # 里对别包类型的隐式引用同样成边。
            m = NS_RE.search(text)
            if m:
                used.append(m.group(1))
            for ns in used:
                own = owners.get(ns, set()) - {p}
                if not own:
                    continue
                if len(owners[ns]) == 1:
                    hits = own
                else:
                    if idents is None:
                        idents = set(IDENT_RE.findall(STRIP_RE.sub(" ", text)))
                    hits = {q for q in own if q != STDLIB and types.get((ns, q), set()) & idents}
                for q in hits - {STDLIB}:
                    if q not in deps[p]:
                        deps[p].add(q)
                        evidence[(p, q)] = f"{f.relative_to(root).as_posix()} (using {ns})"
    return packages, deps, evidence


def sccs(nodes, deps):
    index, low, stack, on, out = {}, {}, [], set(), []
    counter = [0]

    def visit(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on.add(v)
        for w in sorted(deps.get(v, ())):
            if w not in index:
                visit(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = []
            while True:
                w = stack.pop()
                on.discard(w)
                comp.append(w)
                if w == v:
                    break
            out.append(sorted(comp))

    sys.setrecursionlimit(10000)
    for v in nodes:
        if v not in index:
            visit(v)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.add_argument("--graph", action="store_true", help="打印完整依赖图")
    args = ap.parse_args()
    root = Path(args.root)

    packages, deps, evidence = build_graph(root)
    errors = []

    for comp in sccs(packages, deps):
        if len(comp) > 1:
            edges = [
                f"    {a} -> {b}: {evidence[(a, b)]}"
                for a in comp for b in sorted(deps[a]) if b in comp
            ]
            errors.append("dependency cycle: " + " <-> ".join(comp) + "\n" + "\n".join(edges))

    for p in sorted(FOUNDATION):
        for q in sorted(deps.get(p, ())):
            if q not in FOUNDATION:
                errors.append(
                    f"foundation package {p} depends on non-foundation {q}: {evidence[(p, q)]}"
                )

    if args.graph:
        for p in packages:
            print(f"{p} -> {', '.join(sorted(deps.get(p, ()))) or '-'}")

    if errors:
        for e in errors:
            print("package-deps: " + e)
        return 1
    print(f"package-deps OK: {len(packages)} packages, no cycles, foundation layer closed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
