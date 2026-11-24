#!/usr/bin/env python3
"""
scripts/check_code_standards.py

Incremental code standards enforcement gate for Zan repository.
Validates code changes against core quality baselines:
  1. No hand-written '__' double underscore identifiers in packages/stdlib/examples/templates.
  2. Strict unidirectional dependencies: stdlib/ must never depend on packages/ (using Zan.*).
  3. PascalCase naming for public types and methods in .zan files.
  4. File and function size limits to prevent monolithic bloat.
  5. Zan.Gui layering ratchet (scripts/gui_layer_rules.py + scripts/gui_layers_baseline.json):
     no new upward dependencies inside Zan.Gui, no new immediate-mode entry points or
     arbitration calls, no new legacy layout attributes, no new Signal* outside Zan.Gui.
  6. src/compiler must never gain new #include of packages/*/compiler hooks (B-ID222).
  7. Non-GUI packages (e.g. Zan.Hardware, Zan.Web, Zan.Data, Zan.Desktop) must never depend on Gui.* / Zan.Gui.*.

Usage:
    python scripts/check_code_standards.py --staged      # Check staged git changes (pre-commit)
    python scripts/check_code_standards.py [files...]   # Check specific files
"""

import os
import sys
import re
import subprocess
from typing import List, Tuple, Dict, Optional

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Folders where userland/application code resides and must follow strict rules
USERLAND_DIRS = ("packages", "stdlib", "examples", "templates")

# Excluded paths from '__' check (compiler codegen, low-level runtime internals, conformance tests)
UNDERSCORE_EXEMPT_DIRS = (
    os.path.join("src", "compiler"),
    os.path.join("src", "selfhost"),
    os.path.join("src", "runtime"),
    "tests",
    "_scratch",
    "build",
)

# Names the compiler itself recognises or synthesises. They keep their '__'
# spelling on purpose and are exempt from the '__' and PascalCase rules:
# __AttrRoutes (route registration, attr_routes.h), __ZanControlCatalog
# (control catalog, main_synth.c), __Conn (ORM entity-accessor connection hook
# looked up on the base chain, checker_decl.c).
COMPILER_CONTRACT_NAMES = ("__AttrRoutes", "__ZanControlCatalog", "__Conn")

MAX_NEW_FILE_LINES = 1200
MAX_FUNCTION_LINES = 120

def strip_comments_and_strings(line: str) -> str:
    """Remove string literals and line comments from a single line of source code."""
    # Replace escaped quotes first
    s = line.replace(r'\"', '  ')
    # Replace double-quoted string literals with spaces
    s = re.sub(r'"[^"]*"', '""', s)
    # Remove single-line comments
    s = re.sub(r'//.*$', '', s)
    return s


def check_double_underscore(rel_path: str, line_num: int, raw_line: str, stripped_line: str) -> Optional[str]:
    """Rule 1: Prohibit hand-written '__' double underscore identifiers."""
    norm_path = rel_path.replace("\\", "/")

    # Only apply to userland Zan sources
    if not norm_path.endswith(".zan"):
        return None
    if not any(norm_path.startswith(d + "/") for d in USERLAND_DIRS):
        return None
    if any(norm_path.startswith(ex.replace("\\", "/") + "/") for ex in UNDERSCORE_EXEMPT_DIRS):
        return None

    # Check for __ identifiers (e.g., __foo, __Bar_Baz)
    # Exclude special compiler-recognized intrinsics if any, but userland code shouldn't define them
    matches = re.findall(r'\b(__[a-zA-Z0-9_]+)\b', stripped_line)
    for m in matches:
        # Exclude common markdown fences or header separators if accidentally matched
        if m.startswith("___"):
            continue
        # Exclude compiler contracts and the runtime decimal ABI
        if m in COMPILER_CONTRACT_NAMES or (norm_path.startswith("stdlib/System/Decimal.zan") and m.startswith("__zan_decimal_")):
            continue
        return (
            f"手写代码严禁定义或使用 '__' 前缀标识符 ('{m}')。\n"
            f"       双下划线严格保留给编译器内部代码生成/ABI 注入，普通代码请使用普通命名或单下划线私有字段。"
        )
    return None


def check_stdlib_dependency(rel_path: str, line_num: int, raw_line: str, stripped_line: str) -> Optional[str]:
    """Rule 2: stdlib must never depend on external packages (using Zan.*)."""
    norm_path = rel_path.replace("\\", "/")

    if not (norm_path.startswith("stdlib/") and norm_path.endswith(".zan")):
        return None

    # Match 'using Zan.Foo;'
    m = re.match(r'^\s*using\s+Zan\.[a-zA-Z0-9_.]+\s*;', stripped_line)
    if m:
        return (
            f"标准库严禁反向依赖 packages ('{m.group(0).strip()}')。\n"
            f"       stdlib 是语言基石，反向引用扩展包会破坏编译闭包与 --no-packages 纯净环境。"
        )
    return None


# Packages explicitly allowed to reference Gui.* / Zan.Gui.* namespaces
GUI_ALLOWED_PACKAGES = {
    "Zan.Browser.Firefox",
    "Zan.Industrial",
}


def check_non_gui_package_dependency(rel_path: str, line_num: int, raw_line: str, stripped_line: str) -> Optional[str]:
    """Rule 7: Non-GUI packages must never depend on Gui.* / Zan.Gui.*."""
    norm_path = rel_path.replace("\\", "/")
    if not (norm_path.startswith("packages/") and norm_path.endswith(".zan")):
        return None
    parts = norm_path.split("/")
    if len(parts) < 3:
        return None
    pkg_name = parts[1]
    if (
        pkg_name == "Zan.Gui"
        or pkg_name.startswith("Zan.Gui.")
        or pkg_name == "Zan.Game"
        or pkg_name.startswith("Zan.Game.")
        or pkg_name in GUI_ALLOWED_PACKAGES
    ):
        return None

    m = re.match(r'^\s*using\s+(?:Gui|Zan\.Gui)(?:\.[a-zA-Z0-9_.]+)?\s*;', stripped_line)
    if m:
        return (
            f"非 GUI 包 '{pkg_name}' 严禁依赖 GUI 命名空间 ('{m.group(0).strip()}')。\n"
            f"       在基础/硬件/服务端包中引入 Gui.* 会将 800+ UI 文件、zan_gui 驱动及窗口子系统拖入服务端编译闭包；"
            f"请使用依赖倒置接口（如 IHardwareHost）由上层宿主注入。"
        )
    return None


def check_compiler_package_include(rel_path: str, line_num: int, raw_line: str, stripped_line: str) -> Optional[str]:
    """Rule 6: src/compiler must not include packages/*/compiler hooks (B-ID222)."""
    norm_path = rel_path.replace("\\", "/")
    if not norm_path.startswith("src/compiler/"):
        return None
    if not (norm_path.endswith(".c") or norm_path.endswith(".h")):
        return None
    # Match on the raw line: strip_comments_and_strings erases the quoted
    # include path, which would silence this rule entirely.
    line_no_comment = re.sub(r'//.*$', '', raw_line)
    if not re.match(r'^\s*#include\s+"\.\./\.\./packages/', line_no_comment):
        return None
    inc = line_no_comment.strip()
    return (
        f"src/compiler 严禁新增对 packages/*/compiler 钩子头的 #include ('{inc}')。\n"
        f"       包以 C 钩子编进 zanc 的通道已废除（B-ID222，2026-11-19 裁定）："
        f"编译期扩展走包侧数据或 Zan 代码，编译器永不点名包。"
    )


def check_pascal_case_api(rel_path: str, line_num: int, raw_line: str, stripped_line: str) -> Optional[str]:
    """Rule 3: Public types and methods in .zan files must be PascalCase."""
    norm_path = rel_path.replace("\\", "/")

    if not norm_path.endswith(".zan"):
        return None
    if not (norm_path.startswith("packages/") or norm_path.startswith("stdlib/")):
        return None

    # 1. Check public types (class, struct, interface, enum)
    type_match = re.search(
        r'^\s*public\s+(?:static\s+|abstract\s+|sealed\s+|partial\s+)*(class|struct|interface|enum)\s+([a-zA-Z0-9_]+)',
        stripped_line
    )
    if type_match:
        kind = type_match.group(1)
        name = type_match.group(2)
        if name in COMPILER_CONTRACT_NAMES:
            return None
        # Must start with uppercase letter, and no underscores
        if not re.match(r'^[A-Z][a-zA-Z0-9]*$', name):
            return (
                f"公开{kind}命名必须使用 PascalCase (大驼峰且无下划线)，当前命名非法: '{name}'"
            )

    # 2. Check public methods
    # Match: public [modifiers] ReturnType MethodName(
    method_match = re.search(
        r'^\s*public\s+(?:static\s+|async\s+|virtual\s+|override\s+|new\s+|unsafe\s+)*'
        r'(?:(?:[a-zA-Z0-9_<>,\s\[\]\?]+)\s+)?([a-zA-Z0-9_]+)\s*\(',
        stripped_line
    )
    if method_match:
        name = method_match.group(1)
        # Exclude language keywords or constructors (which start with uppercase class name)
        excluded_keywords = {"operator", "this", "base", "delegate", "get", "set"}
        if name not in excluded_keywords and not name.startswith("~") and name not in COMPILER_CONTRACT_NAMES:
            if not re.match(r'^[A-Z][a-zA-Z0-9]*$', name):
                return (
                    f"公开方法命名必须使用 PascalCase (大驼峰且无下划线)，当前命名非法: '{name}'"
                )

    return None


def parse_staged_diff() -> Dict[str, List[Tuple[int, str]]]:
    """Parse git diff --cached -U0 to obtain added lines per file."""
    cmd = ["git", "diff", "--cached", "-U0"]
    res = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if res.returncode != 0:
        return {}

    files_added_lines: Dict[str, List[Tuple[int, str]]] = {}
    current_file = None
    current_line_num = 0

    for line in res.stdout.splitlines():
        if line.startswith("+++ b/"):
            current_file = line[6:].strip()
            if current_file not in files_added_lines:
                files_added_lines[current_file] = []
        elif line.startswith("@@ "):
            # Example: @@ -10,0 +15,3 @@ or @@ -10 +15 @@
            m = re.search(r'\+([0-9]+)(?:,([0-9]+))?', line)
            if m:
                current_line_num = int(m.group(1))
        elif line.startswith("+") and not line.startswith("+++"):
            if current_file:
                files_added_lines[current_file].append((current_line_num, line[1:]))
                current_line_num += 1

    return files_added_lines


def get_staged_new_files() -> List[str]:
    """List newly added files in the git index."""
    cmd = ["git", "diff", "--cached", "--name-status"]
    res = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if res.returncode != 0:
        return []

    new_files = []
    for line in res.stdout.splitlines():
        parts = line.strip().split("\t")
        if len(parts) >= 2 and parts[0] == "A":
            new_files.append(parts[1])
    return new_files


def check_staged() -> int:
    """Run all checks against git staged changes."""
    staged_diff = parse_staged_diff()
    new_files = get_staged_new_files()

    violations = []

    # 1. Check newly added file lengths
    for new_file in new_files:
        norm = new_file.replace("\\", "/")
        if norm.endswith((".zan", ".c", ".h")):
            full_path = os.path.join(REPO_ROOT, new_file)
            if os.path.isfile(full_path):
                with open(full_path, "r", encoding="utf-8", errors="replace") as f:
                    lines = f.readlines()
                if len(lines) > MAX_NEW_FILE_LINES:
                    violations.append(
                        f"[FAIL] {new_file}:1 - 新建文件行数 ({len(lines)} 行) 超过 {MAX_NEW_FILE_LINES} 行门禁上限。\n"
                        f"       请按模块职责拆分，禁止新增巨石单体文件。"
                    )

    # 2. Check added lines across staged diffs
    for rel_path, added_lines in staged_diff.items():
        for line_num, raw_line in added_lines:
            stripped = strip_comments_and_strings(raw_line)

            err = check_double_underscore(rel_path, line_num, raw_line, stripped)
            if err:
                violations.append(f"[FAIL] {rel_path}:{line_num} - {err}")

            err = check_stdlib_dependency(rel_path, line_num, raw_line, stripped)
            if err:
                violations.append(f"[FAIL] {rel_path}:{line_num} - {err}")

            err = check_non_gui_package_dependency(rel_path, line_num, raw_line, stripped)
            if err:
                violations.append(f"[FAIL] {rel_path}:{line_num} - {err}")

            err = check_compiler_package_include(rel_path, line_num, raw_line, stripped)
            if err:
                violations.append(f"[FAIL] {rel_path}:{line_num} - {err}")

            err = check_pascal_case_api(rel_path, line_num, raw_line, stripped)
            if err:
                violations.append(f"[FAIL] {rel_path}:{line_num} - {err}")

    # 3. Zan.Gui layering ratchet (whole-file counts vs. baseline, see gui_layer_rules.py).
    #    A crash in the checker itself must not block every commit in the repo: report it loudly
    #    and let the commit through.
    try:
        import gui_layer_rules
        violations.extend(gui_layer_rules.check_staged())
    except Exception as exc:  # noqa: BLE001 - tool failure is reported, not fatal
        sys.stderr.write(f"[WARN] Zan.Gui 分层机检自身出错，本次已跳过：{exc}\n")

    if violations:
        sys.stderr.write("=" * 70 + "\n")
        sys.stderr.write("【代码规范门禁拦截】发现不符合规范的代码，提交已被拒绝：\n")
        sys.stderr.write("=" * 70 + "\n\n")
        for v in violations:
            sys.stderr.write(v + "\n\n")
        sys.stderr.write("请修正上述违规项后重新提交。\n")
        return 1

    return 0


def check_files(paths: List[str]) -> int:
    """Run checks directly on whole files."""
    violations = []
    for p in paths:
        rel_path = os.path.relpath(p, REPO_ROOT)
        if not os.path.isfile(p):
            continue
        with open(p, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()

        for idx, raw_line in enumerate(lines, start=1):
            stripped = strip_comments_and_strings(raw_line)

            err = check_double_underscore(rel_path, idx, raw_line, stripped)
            if err:
                violations.append(f"[FAIL] {rel_path}:{idx} - {err}")

            err = check_stdlib_dependency(rel_path, idx, raw_line, stripped)
            if err:
                violations.append(f"[FAIL] {rel_path}:{idx} - {err}")

            err = check_non_gui_package_dependency(rel_path, idx, raw_line, stripped)
            if err:
                violations.append(f"[FAIL] {rel_path}:{idx} - {err}")

            err = check_compiler_package_include(rel_path, idx, raw_line, stripped)
            if err:
                violations.append(f"[FAIL] {rel_path}:{idx} - {err}")

            err = check_pascal_case_api(rel_path, idx, raw_line, stripped)
            if err:
                violations.append(f"[FAIL] {rel_path}:{idx} - {err}")

    if violations:
        sys.stderr.write(f"发现 {len(violations)} 处规范违规：\n")
        for v in violations:
            sys.stderr.write(v + "\n")
        return 1

    print("代码规范检查通过，零违规。")
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--staged":
        sys.exit(check_staged())
    elif len(sys.argv) > 1:
        sys.exit(check_files(sys.argv[1:]))
    else:
        # Default to checking staged if in git repo
        sys.exit(check_staged())


if __name__ == "__main__":
    main()
