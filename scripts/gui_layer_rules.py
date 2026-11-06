#!/usr/bin/env python3
"""
scripts/gui_layer_rules.py

Zan.Gui 分层重构的棘轮机检（docs/GUI_ARCHITECTURE.md §4 P0 第 3 步，B-ID171）。
由 scripts/check_code_standards.py --staged 在 pre-commit 时调用；也可单独运行：

    python scripts/gui_layer_rules.py --report     # 全树对照基线，列出超出基线的违规
    python scripts/gui_layer_rules.py --update F…  # 修掉违规后：按工作树重算这些文件的基线条目（锁定收紧）
    python scripts/gui_layer_rules.py --baseline   # 整份重写基线（工作树与 HEAD 逐项取大，仅初建/重置用）

五类规则，全部是"只减不增"的棘轮——现存违规记在 scripts/gui_layers_baseline.json，
提交时逐文件比对暂存版内容，计数超过基线即拒绝：

  layering      Zan.Gui 包内依赖只能向下：按目录判定文件所在层，检查向上的 using、
                类型引用与 Gui.X 限定引用；引用兄弟包（Gui.Hmi、Gui.Component.Chart…）一律违规。
  render_entry  Zan.Gui 的 Widget/Component 不得新增即时模式入口 Render(App …)。
  arbitration   不得新增 App 即时模式输入仲裁调用（ClaimClick、BlockHitsRect…、Ui.Activate*）。
  legacy_attr   设计器文档（带 data-zan-design）以外的 html 不得新增 data-dock/fx/fy/fw/fh/span。
  signal_new    Zan.Gui 以外不得新增 new SignalInt/SignalBool/SignalString 或 Signal.Int/Bool/String。

基线只许降不许升：暂存了基线文件且某类总数高于 HEAD 时拒绝，除非设置环境变量
ZAN_GUI_BASELINE_RAISE=1 并在提交说明写明理由。纯移动文件（git mv）按旧路径的基线比对，
不必重写基线。
"""

import json
import os
import re
import subprocess
import sys
from collections import Counter
from typing import Dict, Iterable, List, Optional, Set, Tuple

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE_REL = "scripts/gui_layers_baseline.json"
GUI_ROOT = "packages/Zan.Gui/src/Gui/"
SKIP_TOP = {"_scratch", "build", "dist", ".git", "node_modules", ".zan-packages"}
DOC = "docs/GUI_ARCHITECTURE.md"

CATEGORIES = ("layering", "render_entry", "arbitration", "legacy_attr", "signal_new")
CATEGORY_DESC = {
    "layering": "Zan.Gui 向上依赖",
    "render_entry": "即时模式入口 Render(App …)",
    "arbitration": "App 即时模式输入仲裁调用",
    "legacy_attr": "旧布局方言属性",
    "signal_new": "Zan.Gui 外新建 Signal*",
}

# 目录（packages/Zan.Gui/src/Gui/ 下第一段；"" 为根目录文件）→ 层级。
# 同级之间允许互相引用；Testing 是旁路层：它可以依赖任何层，任何层都不得依赖它。
LAYER_LEVELS = {
    "Gui": 0,
    "Backend": 1,
    "Rendering": 2, "Text": 2, "Media": 2,
    "Styling": 3, "Layout": 3,
    "Core": 4, "Diagnostics": 4,
    "Binding": 5, "Animation": 5,
    "Widget": 6,
    "Component": 7,
    "Markup": 8,
    "Forms": 9,
}
TESTING = "Testing"
EXTERNAL = "External"

ARBITRATION_APIS = (
    "ClaimClick", "ClickAvailable", "ClickClaimed", "ClaimKey", "KeyConsumed", "KeyBlocked",
    "BlockHitsRect", "ReservePopupHit", "ReserveEdgeHit", "BlockHitsMenu", "BlockHitsBelow",
    "CaptureWheel", "ClaimDrag", "PressOwnsWheel", "PressTarget", "ClickTarget",
    "RightClickTarget", "RightPressTarget", "PointerPressed", "DoubleClickNow", "SwipeDir",
    "SwipeTarget", "LongPressFired", "HeldPressMs", "NoteClickTarget", "PressHitBlocker",
    "PressOnBlocker", "HitTestFrom",
)
ARBITRATION_RE = re.compile(r"\.\s*(" + "|".join(ARBITRATION_APIS) + r")\s*\(")
UI_ACTIVATE_RE = re.compile(r"(?<![\w.])Ui\s*\.\s*(Activate\w*)\s*\(")
RENDER_ENTRY_RE = re.compile(r"(?<![\w.])Render\s*\(\s*App\s+[A-Za-z_]")
SIGNAL_NEW_RE = re.compile(r"(?<![\w.])(?:new\s+(Signal(?:Int|Bool|String))\s*\(|Signal\s*\.\s*(Int|Bool|String)\s*\()")
LEGACY_ATTR_RE = re.compile(r"\b(data-(?:dock|fx|fy|fw|fh|span))\s*=")

USING_RE = re.compile(r"^\s*using\s+(?:static\s+)?([A-Za-z_][\w.]*)\s*;")
NAMESPACE_RE = re.compile(r"^\s*namespace\s+([A-Za-z_][\w.]*)\s*;", re.M)
DECL_RE = re.compile(
    r"^\s*(?:(?:public|internal|private|protected|static|abstract|sealed|partial|readonly|unsafe)\s+)*"
    r"(?:class|struct|interface|enum|record)\s+([A-Z][A-Za-z0-9_]*)", re.M)
DELEGATE_RE = re.compile(
    r"^\s*(?:(?:public|internal|private|protected|static)\s+)*delegate\s+[\w<>\[\],.? ]+?\s+([A-Z][A-Za-z0-9_]*)\s*[<(]",
    re.M)
IDENT_RE = re.compile(r"(?<![\w.])([A-Z][A-Za-z0-9_]*)")
QREF_RE = re.compile(r"(?<![\w.])Gui((?:\s*\.\s*[A-Z][A-Za-z0-9_]*)+)")
# 非 Gui 包文件只需仲裁与 Signal 两类计数：原文不含这些记号就不必逐字剥离注释与字符串。
PREFILTER_RE = re.compile(r"Signal|Ui\s*\.\s*Activate|" + "|".join(ARBITRATION_APIS))
# 出现在这些关键字之后的大写标识符仍是类型用法；出现在普通标识符之后则是声明名
# （如 `EventArgs Empty = …` 里的 Empty），不算类型引用。
KEYWORDS = {
    "new", "return", "is", "as", "case", "typeof", "sizeof", "default", "public", "private",
    "protected", "internal", "static", "readonly", "const", "override", "virtual", "abstract",
    "sealed", "partial", "async", "await", "out", "ref", "in", "params", "throw", "yield", "else",
    "class", "struct", "interface", "enum", "where", "extends", "event", "delegate", "unsafe",
}


def is_type_usage(line: str, start: int, end: int) -> bool:
    """IDENT_RE 命中的标识符是否处在类型用法的位置（而不是方法名或声明名）。"""
    j = end
    while j < len(line) and line[j] in " \t":
        j += 1
    k = start - 1
    while k >= 0 and line[k] in " \t":
        k -= 1
    prev_word = ""
    if k >= 0 and (line[k].isalnum() or line[k] == "_"):
        w = k
        while w >= 0 and (line[w].isalnum() or line[w] == "_"):
            w -= 1
        prev_word = line[w + 1:k + 1]
    if j < len(line) and line[j] == "(" and prev_word != "new":
        return False
    if prev_word and prev_word not in KEYWORDS:
        return False
    if k >= 0 and line[k] in ">]":
        return False
    return True


# ---------------------------------------------------------------------------
# 源码预处理
# ---------------------------------------------------------------------------

def strip_code(text: str) -> str:
    """去掉注释与字符串/字符字面量，保留换行（行号不变）。"""
    out: List[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            if j < 0:
                break
            i = j
            continue
        if c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            end = j + 2 if j >= 0 else n
            out.append("\n" * text.count("\n", i, end))
            i = end
            continue
        if c == '"' or (c in "@$" and (nxt == '"' or (nxt in "@$" and text[i + 2:i + 3] == '"'))):
            k = i
            verbatim = False
            while text[k] != '"':
                verbatim = verbatim or text[k] == "@"
                k += 1
            k += 1
            while k < n:
                ch = text[k]
                if verbatim:
                    if ch == '"':
                        if text[k + 1:k + 2] == '"':
                            k += 2
                            continue
                        break
                elif ch == "\\":
                    k += 2
                    continue
                elif ch == '"' or ch == "\n":
                    break
                k += 1
            out.append('""' + "\n" * text.count("\n", i, min(k + 1, n)))
            i = k + 1
            continue
        if c == "'":
            k = i + 1
            k += 2 if text[k:k + 1] == "\\" else 1
            if text[k:k + 1] == "'":
                out.append("''")
                i = k + 1
                continue
        out.append(c)
        i += 1
    return "".join(out)


def normalize(data: Optional[bytes]) -> Optional[str]:
    if data is None:
        return None
    return data.decode("utf-8", errors="replace").replace("\r\n", "\n")


def norm_rel(path: str) -> str:
    return path.replace("\\", "/")


# ---------------------------------------------------------------------------
# 层与类型表
# ---------------------------------------------------------------------------

def gui_layer_of(rel: str) -> Optional[str]:
    if not rel.startswith(GUI_ROOT) or not rel.endswith(".zan"):
        return None
    rest = rel[len(GUI_ROOT):]
    seg = rest.split("/", 1)[0] if "/" in rest else ""
    if seg == "":
        return "Gui"
    if seg == TESTING or seg in LAYER_LEVELS:
        return seg
    return None


def is_upward(src: str, dst: str) -> bool:
    if dst == src:
        return False
    if dst == EXTERNAL:
        return True
    if dst == TESTING:
        return src != TESTING
    if src == TESTING:
        return False
    return LAYER_LEVELS[dst] > LAYER_LEVELS[src]


class TypeTable:
    """Zan.Gui 各层声明的类型名与命名空间；兄弟包（Gui.* 命名空间）记为 External。"""

    def __init__(self) -> None:
        self.types: Dict[str, Set[str]] = {}
        self.namespaces: Dict[str, Set[str]] = {}

    def resolve_type(self, name: str) -> Optional[str]:
        layers = self.types.get(name)
        return next(iter(layers)) if layers and len(layers) == 1 else None

    def resolve_namespace(self, ns: str) -> Optional[str]:
        layers = self.namespaces.get(ns)
        return next(iter(layers)) if layers and len(layers) == 1 else None

    @staticmethod
    def build() -> "TypeTable":
        table = TypeTable()
        internal: Dict[str, Set[str]] = {}
        external: Set[str] = set()
        ns_internal: Dict[str, Set[str]] = {}
        ns_external: Set[str] = set()
        packages = os.path.join(REPO_ROOT, "packages")
        for pkg in sorted(os.listdir(packages)):
            # Gui.* 命名空间按约定住在 src/Gui/ 下；只扫这一棵，pre-commit 才快得起来。
            src = os.path.join(packages, pkg, "src", "Gui")
            if not os.path.isdir(src):
                continue
            own = pkg == "Zan.Gui"
            for dirpath, _dirs, files in os.walk(src):
                for fname in files:
                    if not fname.endswith(".zan"):
                        continue
                    full = os.path.join(dirpath, fname)
                    with open(full, "r", encoding="utf-8", errors="replace") as f:
                        text = f.read()
                    ns_match = NAMESPACE_RE.search(text)
                    ns = ns_match.group(1) if ns_match else None
                    if own:
                        layer = gui_layer_of(norm_rel(os.path.relpath(full, REPO_ROOT)))
                        if layer is None:
                            continue
                        if ns:
                            ns_internal.setdefault(ns, set()).add(layer)
                        for name in DECL_RE.findall(text) + DELEGATE_RE.findall(text):
                            internal.setdefault(name, set()).add(layer)
                    elif ns and (ns == "Gui" or ns.startswith("Gui.")):
                        ns_external.add(ns)
                        external.update(DECL_RE.findall(text))
                        external.update(DELEGATE_RE.findall(text))
        table.types = dict(internal)
        for name in external:
            table.types.setdefault(name, {EXTERNAL})
        table.namespaces = dict(ns_internal)
        for ns in ns_external:
            table.namespaces.setdefault(ns, {EXTERNAL})
        return table


# ---------------------------------------------------------------------------
# 单文件度量
# ---------------------------------------------------------------------------

def in_scope(rel: str) -> bool:
    if rel.split("/", 1)[0] in SKIP_TOP:
        return False
    return rel.endswith(".zan") or rel.endswith(".html")


def needs_type_table(rel: str) -> bool:
    return gui_layer_of(rel) is not None


def file_metrics(rel: str, text: str, table: Optional[TypeTable]) -> Dict[str, Counter]:
    metrics = {cat: Counter() for cat in CATEGORIES}
    if rel.endswith(".html"):
        if "data-zan-design" not in text:
            for attr in LEGACY_ATTR_RE.findall(text):
                metrics["legacy_attr"][attr] += 1
        return metrics
    if not rel.endswith(".zan"):
        return metrics

    layer = gui_layer_of(rel)
    if layer is None and not PREFILTER_RE.search(text):
        return metrics
    code = strip_code(text)
    for m in ARBITRATION_RE.finditer(code):
        metrics["arbitration"][m.group(1)] += 1
    for m in UI_ACTIVATE_RE.finditer(code):
        metrics["arbitration"]["Ui." + m.group(1)] += 1

    if not rel.startswith("packages/Zan.Gui/"):
        for m in SIGNAL_NEW_RE.finditer(code):
            metrics["signal_new"][m.group(1) or "Signal." + m.group(2)] += 1

    if layer is None:
        return metrics
    if layer in ("Widget", "Component"):
        count = len(RENDER_ENTRY_RE.findall(code))
        if count:
            metrics["render_entry"]["Render(App"] += count
    if table is None:
        return metrics

    layering = metrics["layering"]
    for line in code.split("\n"):
        stripped = line.strip()
        if stripped.startswith("namespace "):
            continue
        um = USING_RE.match(line)
        if um:
            dst = table.resolve_namespace(um.group(1))
            if dst and is_upward(layer, dst):
                layering["using %s -> %s" % (um.group(1), dst)] += 1
            continue
        for qm in QREF_RE.finditer(line):
            parts = ["Gui"] + [p.strip() for p in qm.group(1).split(".") if p.strip()]
            for k in range(len(parts), 0, -1):
                ns = ".".join(parts[:k])
                dst = table.resolve_namespace(ns)
                if dst:
                    if is_upward(layer, dst):
                        layering["qref %s -> %s" % (ns, dst)] += 1
                    break
        for im in IDENT_RE.finditer(line):
            name = im.group(1)
            dst = table.resolve_type(name)
            if dst and is_upward(layer, dst) and is_type_usage(line, im.start(1), im.end(1)):
                layering["type %s -> %s" % (name, dst)] += 1
    return metrics


# ---------------------------------------------------------------------------
# git 读取
# ---------------------------------------------------------------------------

def git(args: List[str], data: Optional[bytes] = None) -> bytes:
    res = subprocess.run(["git"] + args, cwd=REPO_ROOT, input=data, capture_output=True)
    if res.returncode != 0:
        raise RuntimeError("git %s 失败: %s" % (" ".join(args), res.stderr.decode("utf-8", "replace").strip()))
    return res.stdout


def git_batch_read(specs: List[str]) -> Dict[str, Optional[bytes]]:
    """一个 git cat-file --batch 进程读多个对象（如 ':path'、'HEAD:path'）。"""
    result: Dict[str, Optional[bytes]] = {}
    if not specs:
        return result
    out = git(["cat-file", "--batch"], ("\n".join(specs) + "\n").encode("utf-8"))
    pos = 0
    for spec in specs:
        nl = out.index(b"\n", pos)
        header = out[pos:nl].decode("utf-8", "replace")
        pos = nl + 1
        if header.endswith(" missing") or header.endswith(" ambiguous"):
            result[spec] = None
            continue
        size = int(header.split(" ")[2])
        result[spec] = out[pos:pos + size]
        pos += size + 1
    return result


def staged_entries() -> List[Tuple[str, str, Optional[str]]]:
    """(状态, 路径, 重命名前路径)；删除项不返回。"""
    raw = git(["diff", "--cached", "--name-status", "-z", "-M"]).decode("utf-8", "replace")
    fields = raw.split("\0")
    entries: List[Tuple[str, str, Optional[str]]] = []
    i = 0
    while i < len(fields) and fields[i]:
        status = fields[i]
        if status[0] in "RC":
            entries.append((status[0], norm_rel(fields[i + 2]), norm_rel(fields[i + 1])))
            i += 3
        else:
            if status[0] != "D":
                entries.append((status[0], norm_rel(fields[i + 1]), None))
            i += 2
    return entries


def load_baseline_text(text: Optional[str]) -> Dict[str, Dict[str, Dict[str, int]]]:
    if not text:
        return {cat: {} for cat in CATEGORIES}
    data = json.loads(text)
    cats = data.get("categories", {})
    return {cat: cats.get(cat, {}) for cat in CATEGORIES}


def totals(baseline: Dict[str, Dict[str, Dict[str, int]]]) -> Dict[str, int]:
    return {cat: sum(sum(keys.values()) for keys in baseline[cat].values()) for cat in CATEGORIES}


# ---------------------------------------------------------------------------
# pre-commit 入口
# ---------------------------------------------------------------------------

def check_staged() -> List[str]:
    entries = [e for e in staged_entries() if in_scope(e[1]) or e[1] == BASELINE_REL]
    if not entries:
        return []
    specs = [":" + BASELINE_REL, "HEAD:" + BASELINE_REL] + [":" + e[1] for e in entries if in_scope(e[1])]
    blobs = git_batch_read(specs)
    baseline = load_baseline_text(normalize(blobs[":" + BASELINE_REL]))
    table = TypeTable.build() if any(needs_type_table(e[1]) for e in entries) else None

    violations: List[str] = []
    for _status, path, old in entries:
        if not in_scope(path):
            continue
        text = normalize(blobs.get(":" + path))
        if text is None:
            continue
        metrics = file_metrics(path, text, table)
        for cat in CATEGORIES:
            base = baseline[cat].get(path)
            if base is None and old is not None:
                base = baseline[cat].get(old)
            base = base or {}
            for key, count in sorted(metrics[cat].items()):
                allowed = base.get(key, 0)
                if count > allowed:
                    violations.append(
                        "[FAIL] %s - %s：%s 现 %d 处，基线 %d 处。\n"
                        "       Zan.Gui 分层重构的棘轮只减不增（%s）。请改为不新增的写法；"
                        "确属必要时运行 python scripts/gui_layer_rules.py --update <文件> 更新基线，"
                        "基线总数上升需设置 ZAN_GUI_BASELINE_RAISE=1 并在提交说明写明理由。"
                        % (path, CATEGORY_DESC[cat], key, count, allowed, DOC))

    if any(e[1] == BASELINE_REL for e in entries) and os.environ.get("ZAN_GUI_BASELINE_RAISE") != "1":
        head_text = normalize(blobs["HEAD:" + BASELINE_REL])
        if head_text is not None:
            before = totals(load_baseline_text(head_text))
            after = totals(baseline)
            for cat in CATEGORIES:
                if after[cat] > before[cat]:
                    violations.append(
                        "[FAIL] %s - %s 基线总数从 %d 升到 %d。基线只许降不许升；"
                        "确属必要请设置 ZAN_GUI_BASELINE_RAISE=1 并在提交说明写明理由。"
                        % (BASELINE_REL, CATEGORY_DESC[cat], before[cat], after[cat]))
    return violations


# ---------------------------------------------------------------------------
# 全树：报告与重写基线
# ---------------------------------------------------------------------------

def worktree_files() -> List[str]:
    files: List[str] = []
    for top in sorted(os.listdir(REPO_ROOT)):
        if top in SKIP_TOP or top.startswith("."):
            continue
        base = os.path.join(REPO_ROOT, top)
        if os.path.isfile(base):
            rel = norm_rel(top)
            if in_scope(rel):
                files.append(rel)
            continue
        for dirpath, dirs, names in os.walk(base):
            dirs[:] = [d for d in dirs if not d.startswith(".")]
            for name in names:
                rel = norm_rel(os.path.relpath(os.path.join(dirpath, name), REPO_ROOT))
                if in_scope(rel):
                    files.append(rel)
    return files


def collect(files: Iterable[str], reader, table: TypeTable) -> Dict[str, Dict[str, Counter]]:
    result: Dict[str, Dict[str, Counter]] = {}
    for rel in files:
        text = reader(rel)
        if text is None:
            continue
        metrics = file_metrics(rel, text, table)
        if any(metrics[cat] for cat in CATEGORIES):
            result[rel] = metrics
    return result


def read_worktree(rel: str) -> Optional[str]:
    try:
        with open(os.path.join(REPO_ROOT, rel), "rb") as f:
            return normalize(f.read())
    except OSError:
        return None


def write_baseline() -> int:
    table = TypeTable.build()
    current = collect(worktree_files(), read_worktree, table)
    head_files = [norm_rel(p) for p in git(["ls-tree", "-r", "--name-only", "-z", "HEAD"]).decode("utf-8", "replace").split("\0") if p]
    head_files = [p for p in head_files if in_scope(p)]
    head_blobs = git_batch_read(["HEAD:" + p for p in head_files])
    head = collect(head_files, lambda rel: normalize(head_blobs.get("HEAD:" + rel)), table)

    categories: Dict[str, Dict[str, Dict[str, int]]] = {cat: {} for cat in CATEGORIES}
    for rel in sorted(set(current) | set(head)):
        for cat in CATEGORIES:
            merged = Counter()
            for source in (current.get(rel), head.get(rel)):
                if source:
                    for key, count in source[cat].items():
                        merged[key] = max(merged[key], count)
            if merged:
                categories[cat][rel] = dict(sorted(merged.items()))
    save_baseline(categories)
    print_totals(categories)
    return 0


def save_baseline(categories: Dict[str, Dict[str, Dict[str, int]]]) -> None:
    data = {
        "version": 1,
        "note": "Zan.Gui 分层重构棘轮基线（%s）。只许降不许升；由 scripts/gui_layer_rules.py 生成"
                "（--update 定点收紧，--baseline 整份重建），勿手改。" % DOC,
        "categories": {cat: dict(sorted(categories[cat].items())) for cat in CATEGORIES},
    }
    with open(os.path.join(REPO_ROOT, BASELINE_REL), "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, ensure_ascii=False, indent=1, sort_keys=False)
        f.write("\n")


def update_baseline(paths: List[str]) -> int:
    """按工作树内容重算给定文件的基线条目；文件已不存在则删除其条目（git mv 时新旧路径都传）。"""
    with open(os.path.join(REPO_ROOT, BASELINE_REL), "r", encoding="utf-8") as f:
        categories = load_baseline_text(f.read())
    rels = [norm_rel(os.path.relpath(os.path.abspath(p), REPO_ROOT)) for p in paths]
    table = TypeTable.build() if any(needs_type_table(r) for r in rels) else None
    for rel in rels:
        for cat in CATEGORIES:
            categories[cat].pop(rel, None)
        text = read_worktree(rel) if in_scope(rel) else None
        if text is None:
            continue
        metrics = file_metrics(rel, text, table)
        for cat in CATEGORIES:
            if metrics[cat]:
                categories[cat][rel] = dict(sorted(metrics[cat].items()))
    save_baseline(categories)
    print_totals(categories)
    return 0


def print_totals(categories: Dict[str, Dict[str, Dict[str, int]]]) -> None:
    for cat in CATEGORIES:
        files = categories[cat]
        print("%-13s %-28s 文件 %4d  处 %5d" % (cat, CATEGORY_DESC[cat], len(files), sum(sum(v.values()) for v in files.values())))


def report() -> int:
    with open(os.path.join(REPO_ROOT, BASELINE_REL), "r", encoding="utf-8") as f:
        baseline = load_baseline_text(f.read())
    table = TypeTable.build()
    current = collect(worktree_files(), read_worktree, table)
    exceeded = 0
    reducible = Counter()
    for rel, metrics in sorted(current.items()):
        for cat in CATEGORIES:
            base = baseline[cat].get(rel, {})
            for key, count in sorted(metrics[cat].items()):
                if count > base.get(key, 0):
                    exceeded += 1
                    print("[超出] %s - %s：%s 现 %d 处，基线 %d 处" % (rel, CATEGORY_DESC[cat], key, count, base.get(key, 0)))
    for cat in CATEGORIES:
        for rel, keys in baseline[cat].items():
            now = current.get(rel, {}).get(cat, Counter()) if rel in current else Counter()
            for key, allowed in keys.items():
                if now.get(key, 0) < allowed:
                    reducible[cat] += allowed - now.get(key, 0)
    print_totals(baseline)
    if any(reducible.values()):
        print("可收紧：" + "，".join("%s 少 %d 处" % (cat, n) for cat, n in reducible.items() if n) +
              "（对改动文件运行 --update 锁定）")
    print("超出基线 %d 项。" % exceeded)
    return 1 if exceeded else 0


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--baseline":
        return write_baseline()
    if len(sys.argv) > 1 and sys.argv[1] == "--update":
        return update_baseline(sys.argv[2:])
    if len(sys.argv) > 1 and sys.argv[1] == "--report":
        return report()
    violations = check_staged()
    for v in violations:
        sys.stderr.write(v + "\n\n")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
