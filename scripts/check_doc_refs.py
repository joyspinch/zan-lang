#!/usr/bin/env python3
"""
scripts/check_doc_refs.py

Guard against documentation & comment reference drift in Zan codebase.
Focuses on `.zan` sources and core documentation to prevent dead paths
(e.g., migrated paths like `stdlib/Gui/`, `stdlib/Game/`, deleted files).

Usage:
    python scripts/check_doc_refs.py [files or dirs...]
    python scripts/check_doc_refs.py --all
"""

import os
import sys
import re

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EXCLUDE_DIRS = {
    ".git",
    "build",
    "dist",
    "_scratch",
    "node_modules",
    ".zan-packages",
    os.path.join("docs", "archive"),
    os.path.join("src", "runtime", "libwebp"),
    os.path.join("src", "runtime", "nanosvg"),
}

# Generic placeholders / protocols / upstream sources often cited in comments
IGNORED_TOKENS = {
    "tools/call", "tools/list", "src/alt", "src/buf", "src/dstAngle",
    "stdlib/X/Y", "src/main.zan", "src/App.zan", "src/App.html", "src/MainWindow.html",
    "src/Order.zan", "src/Slots.zan", "src/TableUi.zan", "src/controller", "src/model",
    "src/Controller", "src/Model", "src/Dao", "src/Feature", "src/Framework",
}

# Regex to capture repo relative paths
PATH_PATTERN = re.compile(r'(?<![A-Za-z0-9_=\'\"/])((?:stdlib|packages|src|docs|tools|templates|examples|tests|scripts)/[A-Za-z0-9_./\-]+)')

def clean_token(token):
    # Strip line numbers like :123 or :123:45
    token = re.sub(r':\d+(?::\d+)?$', '', token)
    # Strip trailing punctuation
    token = token.rstrip('.,;:?!"\')>]`')
    # Strip common file extensions or keep
    if token.endswith(('.zan', '.c', '.h', '.md', '.txt', '.json', '.pkg', '.py', '.ps1', '.cmake', '.sh')):
        return token
    return token.rstrip('/')

def is_ignored(raw_token, token):
    if raw_token in IGNORED_TOKENS or token in IGNORED_TOKENS:
        return True
    if any(char in raw_token for char in ('*', '<', '>', '{', '}', '$', '%')):
        return True
    if '...' in raw_token:
        return True
    # Ignore prefix tokens ending with dot like 'packages/Zan.'
    if raw_token.endswith('.'):
        return True
    # Ignore upstream JS/TS port references like 'src/coord/axisDefault.ts'
    if token.endswith(('.js', '.ts')):
        return True
    return False

def check_file(file_path, violations):
    rel_file = os.path.relpath(file_path, REPO_ROOT).replace('\\', '/')
    
    for exc in EXCLUDE_DIRS:
        norm_exc = exc.replace('\\', '/')
        if rel_file == norm_exc or rel_file.startswith(norm_exc + '/'):
            return

    # Check if this file is inside a package
    pkg_root = None
    parts = rel_file.split('/')
    if len(parts) >= 2 and parts[0] == 'packages':
        pkg_root = os.path.join(REPO_ROOT, parts[0], parts[1])

    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line_no, line in enumerate(f, 1):
                if 'src="' in line or "src='" in line:
                    continue

                is_comment = False
                if file_path.endswith(('.zan', '.c', '.h')):
                    stripped = line.strip()
                    if stripped.startswith(('//', '/*', '*', '///')):
                        is_comment = True
                elif file_path.endswith('.md'):
                    is_comment = True
                
                if not is_comment:
                    continue

                for match in PATH_PATTERN.finditer(line):
                    raw_token = match.group(1)
                    token = clean_token(raw_token)
                    if not token or is_ignored(raw_token, token):
                        continue
                    
                    # 1. Try repo root relative
                    target_abs = os.path.join(REPO_ROOT, token.replace('/', os.sep))
                    if os.path.exists(target_abs):
                        continue

                    # 2. Package-relative resolution
                    if pkg_root and token.startswith('src/'):
                        pkg_target_abs = os.path.join(pkg_root, token.replace('/', os.sep))
                        if os.path.exists(pkg_target_abs):
                            continue

                    violations.append((rel_file, line_no, raw_token, token))
    except Exception:
        pass

def main():
    target_files = []
    args = sys.argv[1:]
    
    if args and args[0] != '--all':
        for arg in args:
            if os.path.isfile(arg):
                target_files.append(os.path.abspath(arg))
            elif os.path.isdir(arg):
                for root, dirs, files in os.walk(arg):
                    dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
                    for f in files:
                        if f.endswith(('.zan', '.c', '.h', '.md')):
                            target_files.append(os.path.join(root, f))
    else:
        # Default scan: .zan in stdlib & packages, and key docs in docs/
        scan_roots = [
            os.path.join(REPO_ROOT, "stdlib"),
            os.path.join(REPO_ROOT, "packages"),
        ]
        for sroot in scan_roots:
            if not os.path.exists(sroot):
                continue
            for root, dirs, files in os.walk(sroot):
                dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
                for f in files:
                    if f.endswith('.zan'):
                        target_files.append(os.path.join(root, f))
        
        # Add core root docs
        core_docs = [
            os.path.join(REPO_ROOT, "docs", "STDLIB.md"),
            os.path.join(REPO_ROOT, "docs", "WORKSPACE_CONVENTIONS.md"),
            os.path.join(REPO_ROOT, "docs", "agent-kb", "project-map.md"),
        ]
        for cd in core_docs:
            if os.path.isfile(cd):
                target_files.append(cd)

    violations = []
    for fp in target_files:
        check_file(fp, violations)

    if violations:
        print(f"FAILED: Found {len(violations)} dead path references in comments/docs:")
        for rel_file, line_no, raw, token in violations:
            print(f"  {rel_file}:{line_no} -> '{raw}' (resolved '{token}' does not exist)")
        sys.exit(1)
    else:
        print(f"PASSED: Verified doc/comment path references across {len(target_files)} files. 0 dead paths found.")
        sys.exit(0)

if __name__ == '__main__':
    main()
