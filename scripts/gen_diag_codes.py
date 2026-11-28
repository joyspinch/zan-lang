#!/usr/bin/env python3
"""
单源诊断编号生成器（B-ID187）。

解析 docs/DIAGNOSTIC_CODES.md 中的 Markdown 编号表，同源生成：
  1. src/compiler/diag_codes.h        (C11 枚举常量与级别查询)
  2. src/selfhost/frontend/diag_codes.zan (自举编译器 DiagCodes 常量与前缀格式化)

用法：
  python scripts/gen_diag_codes.py           # 生成/覆盖两个产物
  python scripts/gen_diag_codes.py --check   # 校验表完整性与产物零漂移（CI/CTest 门禁）
"""
import argparse
import os
import re
import sys

ROW_RE = re.compile(r"^\|\s*([EW])(\d{4})\s*\|\s*([EW])\s*\|\s*(.+?)\s*\|\s*$")


def parse_table(md_path):
    with open(md_path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()

    entries = []
    seen_nums = {}
    errors = []

    for lineno, line in enumerate(lines, 1):
        if not line.startswith("|"):
            continue
        # Skip table header & separator rows
        if re.match(r"^\|\s*号\s*\|", line) or re.match(r"^\|\s*-+\s*\|", line):
            continue
        m = ROW_RE.match(line)
        if not m:
            errors.append(f"{md_path}:{lineno}: malformed table row: {line}")
            continue
        letter, digits, level, msg = m.group(1), m.group(2), m.group(3), m.group(4)
        code_id = f"{letter}{digits}"
        num = int(digits, 10)
        if letter != level:
            errors.append(
                f"{md_path}:{lineno}: code prefix '{code_id}' disagrees with level column '{level}'"
            )
        if num in seen_nums:
            prev_line, prev_id = seen_nums[num]
            errors.append(
                f"{md_path}:{lineno}: duplicate numeric code {num:04d} ({code_id} collides with {prev_id} at line {prev_line})"
            )
        else:
            seen_nums[num] = (lineno, code_id)
        if not msg.strip():
            errors.append(f"{md_path}:{lineno}: empty message for {code_id}")
        entries.append((code_id, level, num, msg.strip()))

    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        raise SystemExit(1)

    if not entries:
        print(f"{md_path}: no diagnostic rows found", file=sys.stderr)
        raise SystemExit(1)

    entries.sort(key=lambda item: item[2])
    return entries


def sanitize_comment(msg):
    s = msg.replace("*/", "* /").replace("\r", " ").replace("\n", " ")
    if len(s) > 96:
        s = s[:93] + "..."
    return s


def render_c_header(entries):
    warnings = [e for e in entries if e[1] == "W"]
    out = [
        "/* AUTO-GENERATED from docs/DIAGNOSTIC_CODES.md by scripts/gen_diag_codes.py.",
        " * DO NOT EDIT MANUALLY — edit docs/DIAGNOSTIC_CODES.md and re-run the generator. */",
        "#ifndef ZAN_DIAG_CODES_H",
        "#define ZAN_DIAG_CODES_H",
        "",
        "#include <stdbool.h>",
        "",
        f"#define ZAN_DIAG_CODE_COUNT {len(entries)}",
        "",
        "typedef enum {",
        "    ZAN_DIAG_NONE = 0,",
    ]
    for code_id, _level, num, msg in entries:
        out.append(f"    ZAN_DIAG_{code_id} = {num}, /* {sanitize_comment(msg)} */")
    out.extend([
        "} zan_diag_code_id_t;",
        "",
        "static inline bool zan_diag_code_is_warning(int code) {",
        "    switch (code) {",
    ])
    for code_id, _level, _num, _msg in warnings:
        out.append(f"    case ZAN_DIAG_{code_id}:")
    out.extend([
        "        return true;",
        "    default:",
        "        return false;",
        "    }",
        "}",
        "",
        "#endif /* ZAN_DIAG_CODES_H */",
        "",
    ])
    return "\n".join(out)


def render_zan_source(entries):
    warnings = [e for e in entries if e[1] == "W"]
    out = [
        "// AUTO-GENERATED from docs/DIAGNOSTIC_CODES.md by scripts/gen_diag_codes.py.",
        "// DO NOT EDIT MANUALLY — edit docs/DIAGNOSTIC_CODES.md and re-run the generator.",
        "using System;",
        "namespace Zan;",
        "",
        "public class DiagCodes {",
        f"    public const int CodeCount = {len(entries)};",
    ]
    for code_id, _level, num, _msg in entries:
        out.append(f"    public const int {code_id} = {num};")
    out.extend([
        "",
        "    public static string Pad4(int code) {",
        "        string s = Convert.ToString(code);",
        "        while (s.Length < 4) { s = \"0\" + s; }",
        "        return s;",
        "    }",
        "",
        "    public static bool IsWarningCode(int code) {",
    ])
    cond_parts = [f"code == {code_id}" for code_id, _level, _num, _msg in warnings]
    for i in range(0, len(cond_parts), 4):
        chunk = " || ".join(cond_parts[i:i + 4])
        out.append(f"        if ({chunk}) {{ return true; }}")
    out.extend([
        "        return false;",
        "    }",
        "",
        "    public static string FormatError(int code, string message) {",
        "        return \"[E\" + Pad4(code) + \"] \" + message;",
        "    }",
        "",
        "    public static string FormatWarning(int code, string message) {",
        "        return \"[W\" + Pad4(code) + \"] \" + message;",
        "    }",
        "}",
        "",
    ])
    return "\n".join(out)


def write_or_check(path, expected, check_only):
    if check_only:
        if not os.path.exists(path):
            print(f"DRIFT: missing generated file {path}", file=sys.stderr)
            return False
        with open(path, "r", encoding="utf-8", newline="") as f:
            actual = f.read().replace("\r\n", "\n")
        if actual != expected:
            print(f"DRIFT: {path} is out of sync with docs/DIAGNOSTIC_CODES.md (run scripts/gen_diag_codes.py)", file=sys.stderr)
            return False
        return True
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(expected)
    return True


def main():
    ap = argparse.ArgumentParser(description="Generate C and Zan diagnostic code tables from docs/DIAGNOSTIC_CODES.md")
    ap.add_argument("--root", default="", help="Repo root directory (default: parent of scripts/)")
    ap.add_argument("--check", action="store_true", help="Verify generated files match docs/DIAGNOSTIC_CODES.md without writing")
    args = ap.parse_args()

    root = args.root or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    md_path = os.path.join(root, "docs", "DIAGNOSTIC_CODES.md")
    c_path = os.path.join(root, "src", "compiler", "diag_codes.h")
    zan_path = os.path.join(root, "src", "selfhost", "frontend", "diag_codes.zan")

    entries = parse_table(md_path)
    c_text = render_c_header(entries)
    zan_text = render_zan_source(entries)

    ok_c = write_or_check(c_path, c_text, args.check)
    ok_zan = write_or_check(zan_path, zan_text, args.check)
    if args.check:
        if not (ok_c and ok_zan):
            raise SystemExit(1)
        print(f"[PASS] diag_codes sync check ({len(entries)} codes: docs/DIAGNOSTIC_CODES.md == diag_codes.h == diag_codes.zan)")
    else:
        print(f"[OK] generated {c_path} and {zan_path} ({len(entries)} codes)")


if __name__ == "__main__":
    main()
