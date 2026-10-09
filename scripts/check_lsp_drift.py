#!/usr/bin/env python3
"""
Verify that LSP static tables (such as stdlib_namespace_map in src/lsp/intellisense.c)
do not drift from actual compiler built-in APIs in src/compiler/builtin_api.c or stdlib.
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INTELLISENSE_C = os.path.join(REPO_ROOT, "src", "lsp", "intellisense.c")
BUILTIN_API_C = os.path.join(REPO_ROOT, "src", "compiler", "builtin_api.c")
STDLIB_DIR = os.path.join(REPO_ROOT, "stdlib")

def get_builtin_types():
    with open(BUILTIN_API_C, "r", encoding="utf-8") as f:
        content = f.read()
    # Match BT("Name", ...)
    matches = re.findall(r'BT\(\s*"([A-Za-z0-9_]+)"', content)
    return set(matches)

def get_codebase_types():
    types = set()
    dirs_to_scan = [STDLIB_DIR, os.path.join(REPO_ROOT, "packages")]
    for base in dirs_to_scan:
        if not os.path.exists(base):
            continue
        for root, dirs, files in os.walk(base):
            for file in files:
                if file.endswith(".zan"):
                    path = os.path.join(root, file)
                    try:
                        with open(path, "r", encoding="utf-8") as f:
                            text = f.read()
                            found = re.findall(r'\b(?:class|struct|interface|enum)\s+([A-Za-z0-9_]+)\b', text)
                            types.update(found)
                    except Exception:
                        pass
    return types

def parse_lsp_namespace_map():
    with open(INTELLISENSE_C, "r", encoding="utf-8") as f:
        content = f.read()
    
    m = re.search(r'stdlib_namespace_map\[\]\s*=\s*\{([^;]+)\};', content, re.DOTALL)
    if not m:
        print("ERROR: Could not find stdlib_namespace_map in intellisense.c")
        return {}
    
    body = m.group(1)
    # Match {"Namespace", {"Type1", "Type2", ..., NULL}}
    blocks = re.findall(r'\{\s*"([^"]+)"\s*,\s*\{([^}]+)\}\s*\}', body)
    ns_map = {}
    for ns, types_str in blocks:
        types = re.findall(r'"([^"]+)"', types_str)
        ns_map[ns] = [t for t in types if t != "NULL"]
    return ns_map

def main():
    builtins = get_builtin_types()
    codebase_types = get_codebase_types()
    all_known = builtins | codebase_types
    # Add primitives and well-known types
    all_known.update(["String", "Object", "Array", "Type", "Console", "Math", "Environment", "Convert", "GC",
                      "Nullable", "Tuple", "Func", "Action", "List", "Dict", "Set", "Queue", "Stack"])
    
    ns_map = parse_lsp_namespace_map()
    if not ns_map:
        sys.exit(1)
    
    errors = []
    total_types = 0
    for ns, types in ns_map.items():
        for t in types:
            total_types += 1
            if t not in all_known:
                errors.append(f"Type '{t}' in namespace '{ns}' is not found in compiler builtins or stdlib!")

    if errors:
        print(f"LSP Drift Check FAILED: {len(errors)} unknown types:")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)
    else:
        print(f"LSP Drift Check PASSED: verified {total_types} types across {len(ns_map)} namespaces.")
        sys.exit(0)

if __name__ == "__main__":
    main()
