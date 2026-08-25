#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
api_extract.py — 从 Zan 标准库（../stdlib/**/*.zan）提取公开 API 面，
输出 JSON/每命名空间 Markdown，供 zan-site 文档生成器使用。

输出:
  out/ref/index.json   命名空间索引（名称 + 类型名 + 文件列表）
  out/ref/data.json    完整模型（命名空间 -> 类型 -> 成员），供渲染与检索
  out/ref/<ns>.md      每命名空间的 Markdown 参考（签名 + /// 文档注释）

提取是"语法感知的 token 扫描"：
  - 识别 namespace（文件作用域 / 块）、类型声明（class/struct/interface/
    enum/delegate/record）、成员（字段/属性/索引器/方法/构造/析构/运算符/
    事件/嵌套类型），并以 {; 与 } 作为成员边界；
  - /// 行文档注释附着到其后的声明；
  - 字符串（普通/逐字/插值）、字符、注释（可嵌套 /* */）均以 token 处理，
    不会干扰花括号配对。

用法: python gen/api_extract.py [stdlib] [out]
"""
import json
import html as _html
import os
import re
import sys

KEYWORDS = {
    "abstract", "as", "async", "await", "base", "bool", "break", "byte",
    "case", "catch", "char", "checked", "class", "const", "continue",
    "decimal", "default", "delegate", "do", "double", "else", "enum",
    "extern", "false", "finally", "fixed", "float", "for", "foreach",
    "get", "goto", "if", "in", "int", "interface", "internal", "is",
    "let", "lock", "long", "namespace", "new", "nint", "null", "object",
    "operator", "out", "override", "private", "protected", "public",
    "readonly", "ref", "return", "sbyte", "sealed", "set", "short",
    "sizeof", "static", "string", "struct", "switch", "this", "throw",
    "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe",
    "ushort", "using", "var", "virtual", "void", "weak", "when", "where",
    "while",
}
# 上下文关键字（parser 里按 IDENT + 前瞻识别的声明词）
CTX_DECL = {
    "record", "partial", "yield", "init", "implicit", "explicit", "event",
    "params", "not", "nameof", "value",
}
TYPE_DECL_KW = ("class", "struct", "interface", "enum", "delegate", "record")
MODIFIERS = {
    "public", "private", "protected", "internal", "static", "virtual",
    "override", "abstract", "sealed", "readonly", "extern", "async",
    "unsafe", "weak", "partial", "const",
}
MULTI_OPS = [
    "?.", "??", "=>", "==", "!=", "<=", ">=", "&&", "||", "++", "--",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=",
    "<<", ">>", "->", "::",
]


def tokenize(text):
    """把 Zan 源码切成 token 流。token 是 dict：
    {k: ident|kw|num|str|char|punct|comment|doc|directive|nl, t: 文本, line: 行号}"""
    toks = []
    i, n = 0, len(text)
    line = 1

    def push(k, t, startline):
        toks.append({"k": k, "t": t, "line": startline})

    def skip_ws():
        nonlocal i, line
        while i < n:
            c = text[i]
            if c == "\n":
                line += 1
                i += 1
            elif c in " \t\r\f\v":
                i += 1
            else:
                break

    while i < n:
        c = text[i]
        # --- whitespace / newline
        if c == "\n":
            toks.append({"k": "nl", "t": "\n", "line": line})
            line += 1
            i += 1
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        # --- preprocessor directive (whole line)
        if c == "#":
            start = i
            while i < n and text[i] != "\n":
                i += 1
            toks.append({"k": "directive", "t": text[start:i], "line": line})
            continue
        # --- comments
        if text.startswith("//", i):
            doc = text.startswith("///", i)
            start = i
            while i < n and text[i] != "\n":
                i += 1
            body = text[start:i][3:] if doc else text[start:i][2:]
            toks.append({"k": "doc" if doc else "comment", "t": body, "line": line})
            continue
        if text.startswith("/*", i):
            start = i
            bl = line
            depth = 0
            while i < n:
                if text.startswith("/*", i):
                    depth += 1
                    i += 2
                elif text.startswith("*/", i):
                    depth -= 1
                    i += 2
                    if depth == 0:
                        break
                else:
                    if text[i] == "\n":
                        line += 1
                    i += 1
            toks.append({"k": "comment", "t": text[start:i], "line": bl})
            continue
        # --- strings
        if c == '"' or (c in "$@" and i + 1 < n and text[i + 1] == '"'):
            # 普通 / @" 逐字 / $" 插值（$@ 组合不存在，但顺序任意兼容）
            interp = c == "$" or (c != '"' and text[i + 1:].startswith("$"))
            verb = c == "@" or (c != '"' and text[i + 1:].startswith("@"))
            start = i
            bl = line
            if c != '"':
                i += 1  # $ / @ 前缀
                if i < n and text[i] in "$@":
                    i += 1
            # 越过开头的引号
            if i < n and text[i] == '"':
                i += 1
            # 消耗到结尾引号
            if verb:
                while i < n:
                    if text[i] == '"':
                        if i + 1 < n and text[i + 1] == '"':
                            i += 2
                            continue
                        i += 1
                        break
                    if text[i] == "\n":
                        line += 1
                    i += 1
            else:
                brace = 0
                while i < n:
                    ch = text[i]
                    if ch == "\\":
                        i += 2
                        continue
                    if ch in "\r\n":
                        # 普通字符串不跨行（字面换行非法），但为了健壮性继续
                        if ch == "\n":
                            line += 1
                        i += 1
                        continue
                    if interp:
                        if ch == "{":
                            if i + 1 < n and text[i + 1] == "{":
                                i += 2
                                continue
                            brace += 1
                            i += 1
                            continue
                        if ch == "}":
                            if i + 1 < n and text[i + 1] == "}":
                                i += 2
                                continue
                            if brace > 0:
                                brace -= 1
                                i += 1
                                continue
                            # 无配对的大括号 —— 可能是格式说明符后的收尾
                            i += 1
                            continue
                        if ch == '"':
                            if brace == 0:
                                i += 1
                                break
                            # 大括号内的嵌套字符串
                            str_start = i
                            i += 1
                            while i < n:
                                if text[i] == "\\":
                                    i += 2
                                    continue
                                if text[i] == '"':
                                    i += 1
                                    break
                                if text[i] == "\n":
                                    line += 1
                                i += 1
                            continue
                    else:
                        if ch == '"':
                            i += 1
                            break
                    i += 1
            toks.append({"k": "str", "t": text[start:i], "line": bl})
            continue
        # --- char literal (不吞单引号操作符，因为 Zan 没有 ' 运算符)
        if c == "'":
            start = i
            bl = line
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == "'":
                    i += 1
                    break
                if text[i] == "\n":
                    line += 1
                i += 1
            toks.append({"k": "char", "t": text[start:i], "line": bl})
            continue
        # --- numbers
        if c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit()):
            start = i
            while i < n and (text[i].isalnum() or text[i] == "." or text[i] == "_"):
                i += 1
            toks.append({"k": "num", "t": text[start:i], "line": line})
            continue
        # --- identifiers
        if c == "_" or c.isalpha():
            start = i
            while i < n and (text[i].isalnum() or text[i] == "_"):
                i += 1
            word = text[start:i]
            k = "kw" if word in KEYWORDS else "ident"
            toks.append({"k": k, "t": word, "line": line})
            continue
        # --- punctuation
        matched = False
        for op in MULTI_OPS:
            if text.startswith(op, i):
                toks.append({"k": "punct", "t": op, "line": line})
                i += len(op)
                matched = True
                break
        if matched:
            continue
        toks.append({"k": "punct", "t": c, "line": line})
        i += 1
    toks.append({"k": "eof", "t": "", "line": line})
    return toks


def combine(tokens):
    """把 token 列表拼成可读的签名文本。"""
    parts = []
    prev = None
    for tk in tokens:
        t = tk["t"]
        if tk["k"] in ("comment", "doc", "directive", "nl", "eof"):
            continue
        if not t:
            continue
        # 空格规则：标识符/数字/关键字之间留空格；标点紧凑
        need_space = parts and prev is not None
        if prev is not None and (
            prev[-1].isalnum() or prev[-1] == "_" or prev[-1] == ">"
        ) and (t[0].isalnum() or t[0] == "_" or t[0] == "<" or t[0] == "?"):
            parts.append(" ")
        elif prev is not None and prev.endswith(",") and t not in (";",):
            parts.append(" ")
        elif prev is not None and prev in ("if", "for", "while", "catch", "switch") and not t.startswith("("):
            parts.append(" ")
        parts.append(t)
        prev = t
    sig = "".join(parts)
    # 轻度清理：去掉 " (" -> "(" 等
    sig = re.sub(r"\s*\(\s*", "(", sig)
    sig = re.sub(r"\s*\)\s*", ")", sig)
    sig = re.sub(r"\s*\[\s*", "[", sig)
    sig = re.sub(r"\s*\]\s*", "]", sig)
    sig = re.sub(r"\[\s*(\d+|,)\s*\]", lambda m: "[" + m.group(1).replace(" ", "") + "]", sig)
    sig = re.sub(r"\s*,\s*", ", ", sig)
    sig = re.sub(r"\s*\?\s*(\w)", r"?\1", sig)
    sig = re.sub(r"\s*<(\w+)\s*>", r"<\1>", sig)
    sig = re.sub(r"\s*\{", "{", sig)
    sig = re.sub(r"\{\s*", "{ ", sig)
    sig = re.sub(r"\s*;", ";", sig)
    sig = re.sub(r"\s{2,}", " ", sig).strip()
    # 还原关键字与 ( 之间的空格（for ( 、if ( 、while ( 等）
    sig = re.sub(r"\b(for|if|while|catch|switch|return|new)\(", r"\1 (", sig)
    sig = re.sub(r"\b(for|if|while|catch|switch|return|new)\(", r"\1 (", sig)
    return sig


def doc_comment(toks, i):
    """收集 tokens[i] 起的连续 /// 注释（调用方传入的是注释段首 token）。"""
    docs = []
    j = i
    while j < len(toks):
        tk = toks[j]
        if tk["k"] == "doc":
            docs.append(tk["t"])
            j += 1
            continue
        if tk["k"] == "nl":
            # 注释行之间恰好一个换行；空行或紧跟非注释则结束
            if j + 1 < len(toks) and toks[j + 1]["k"] == "doc":
                j += 1
                continue
            break
        break
    # 清洗：去标签、解码字面实体、HTML 标签归一为轻量标记
    lines = []
    for d in docs:
        d = d.strip()
        # 解码一切字面 HTML 实体（&lt; &quot; &#x27; 等），统一成字符形态
        d = _html.unescape(d)
        d = re.sub(r"<summary>\s*", "", d)
        d = re.sub(r"\s*</summary>", "", d)
        d = re.sub(r"<see cref=\"([^\"]+)\"/>", r"`\1`", d)
        d = re.sub(r"<param name=\"[^\"]+\">", "", d)
        d = re.sub(r"</param>", "", d)
        d = re.sub(r"<returns>\s*", "", d)
        d = re.sub(r"\s*</returns>", "", d)
        # 成对的富文本标签 → 轻量标记
        d = re.sub(r"<code>(.*?)</code>", r"`\1`", d, flags=re.S)
        d = re.sub(r"<b>(.*?)</b>", r"**\1**", d, flags=re.S)
        d = re.sub(r"<i>(.*?)</i>", r"*\1*", d, flags=re.S)
        d = re.sub(r"<strong>(.*?)</strong>", r"**\1**", d, flags=re.S)
        d = re.sub(r"<em>(.*?)</em>", r"*\1*", d, flags=re.S)
        d = re.sub(r"<br\s*/?>", "\n", d, flags=re.I)
        # 未配对的 HTML 标签（程序员手写的 <code>/.，无闭合）→ 剥掉
        d = re.sub(r"</?(?:code|b|i|strong|em|p|div|span|ul|\bli\b|ol|pre|h[1-6])\b[^>]*>",
                   "", d, flags=re.I)
        lines.append(d)
    text = "\n".join(lines).strip()
    return text


def parse_attrs(toks, i):
    """从 toks[i] 起解析 [ ... ] 属性列表（可能多组），返回 (文本, 结束索引)。"""
    if i >= len(toks) or toks[i]["t"] != "[":
        return "", i
    parts = []
    while i < len(toks) and toks[i]["t"] == "[":
        depth = 0
        start = i
        while i < len(toks):
            tk = toks[i]
            if tk["k"] in ("nl", "eof"):
                pass
            if tk["t"] == "[":
                depth += 1
            elif tk["t"] == "]":
                depth -= 1
                if depth == 0:
                    i += 1
                    break
            i += 1
        parts.append(combine(toks[start:i]))
    return ", ".join(p for p in parts if p), i


def skip_to_semicolon(toks, i):
    while i < len(toks) and toks[i]["k"] != "eof" and toks[i]["t"] != ";":
        i += 1
    return min(i + 1, len(toks))


def find_matching(toks, i):
    """toks[i] 是 '{'；返回配对的 '}' 的索引+1。"""
    depth = 0
    while i < len(toks):
        tk = toks[i]
        if tk["k"] == "eof":
            return i
        if tk["t"] == "{":
            depth += 1
        elif tk["t"] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def type_params_of(toks, i):
    """toks[i] 起若为 '<...>' 则吞掉（含泛型约束）返回 (文本, 新索引)。"""
    if i >= len(toks) or toks[i]["t"] != "<":
        return "", i
    depth = 0
    parts = []
    while i < len(toks):
        tk = toks[i]
        if tk["k"] in ("nl", "eof", "comment", "doc"):
            i += 1
            continue
        if tk["t"] == "<":
            depth += 1
        elif tk["t"] == ">":
            depth -= 1
            if depth == 0:
                i += 1
                break
        parts.append(tk)
        i += 1
    return combine(parts), i


def parse_type_header(toks, i):
    """解析类型头：期望 toks[i] 为 class/struct/enum/...；返回 dict 或 None。"""
    ki = i
    headers = []
    while ki < len(toks):
        tk = toks[ki]
        if tk["k"] == "nl":
            headers.append(tk)
            ki += 1
            continue
        break
    t = toks[i]
    if t["k"] != "kw" and t["t"] not in ("record",):
        return None
    if t["t"] not in TYPE_DECL_KW:
        return None
    kind = t["t"]
    j = i + 1
    # 跳过换行找名字
    while j < len(toks) and toks[j]["k"] == "nl":
        j += 1
    if j >= len(toks) or toks[j]["k"] not in ("ident", "kw"):
        return None
    name = toks[j]["t"]
    j += 1
    tp, j = type_params_of(toks, j)
    # 基类 / 实现接口
    bases = []
    if j < len(toks) and toks[j]["t"] == ":":
        j += 1
        depth = 0
        parts = []
        while j < len(toks):
            tk = toks[j]
            if tk["k"] in ("nl",):
                j += 1
                continue
            if tk["t"] == "{":
                break
            if tk["t"] == ";":
                break
            if tk["k"] == "eof":
                break
            parts.append(tk)
            j += 1
        if parts:
            raw = combine(parts)
            base_list = [b.strip() for b in raw.split(",") if b.strip()]
            bases = base_list
    return {"kind": kind, "name": name, "tparams": tp, "bases": bases, "i": j}


def clean_sig(sig):
    """用于做文档显示的最后清理。"""
    sig = re.sub(r"\s*\{\s*\.\.\.*\s*\}\s*$", "", sig)
    sig = re.sub(r"\s+", " ", sig).strip()
    return sig


class Extractor:
    def __init__(self):
        self.namespaces = {}  # ns -> dict(types=[...], files=set)
        self.order = []
        self.file_ns = ""

    def ns_of(self, stack, file_ns):
        """命名空间计算；无显式 namespace 且无块 ns 时按目录推断
        （stdlib/Gui/ChildWindow.zan 这类文件 → Gui）。"""
        ns = file_ns
        if not ns and not stack:
            ns = self.infer_ns
        parts = []
        if ns:
            parts.append(ns)
        for p in stack:
            parts.append(p)
        return ".".join(parts)

    def ensure_ns(self, ns, fname):
        if ns not in self.namespaces:
            self.namespaces[ns] = {"types": [], "files": []}
            self.order.append(ns)
        if fname not in self.namespaces[ns]["files"]:
            self.namespaces[ns]["files"].append(fname)

    def add_type(self, ns, t, fname):
        self.ensure_ns(ns, fname)
        self.namespaces[ns]["types"].append(t)

    # ------------------------------------------------------------------ #
    def parse_file(self, path):
        with open(path, "r", encoding="utf-8-sig", errors="replace") as f:
            text = f.read()
        toks = tokenize(text)
        rel = os.path.relpath(path, self.stdlib).replace("\\", "/")
        self.file_ns = ""  # 每个文件独立
        # 目录推断命名空间：stdlib/Gui/ChildWindow.zan -> Gui（无显式 namespace 时用）
        parts = rel.split("/")
        self.infer_ns = ".".join(parts[:-1]) if len(parts) > 1 else ""
        self._walk(toks, rel, 0, [])

    def _walk(self, toks, fname, i, ns_stack, file_ns=None, type_ctx=None, end_i=None):
        """顶层 / 类型体扫描。
        type_ctx: 当前类型 dict 或 None（顶层）。end_i: 类型体结束索引（不含）。"""
        if end_i is None:
            end_i = len(toks)
        stack = list(ns_stack) if ns_stack else []
        guard = 0
        while i < end_i:
            # file_ns 必须是动态值：文件作用域命名空间在遍历中途才被识别。
            file_ns = self.file_ns
            guard += 1
            if guard > (end_i - i) * 20 + 2000:
                raise RuntimeError(f"walk guard @{i} line {toks[i].get('line')} fname={fname}")
            tk = toks[i]
            if tk["k"] == "eof":
                break
            if tk["k"] in ("nl", "comment"):
                i += 1
                continue
            if tk["k"] == "directive":
                # 指令对文档不敏感，仅贯穿
                i += 1
                continue
            if tk["k"] == "doc":
                # 收集文档注释，与其后的声明绑定
                docs = doc_comment(toks, i)
                i = self._skip_comment_run(toks, i)
                if i >= end_i:
                    break
                # 把 docs 附着到接下来的声明 —— 通过临时注入
                # 简化：递归处理（重入时从 i 开始, 以 docs 参数附带）
                i = self._walk_decl(toks, fname, i, stack, file_ns, type_ctx, end_i, docs)
                continue
            i = self._walk_decl(toks, fname, i, stack, file_ns, type_ctx, end_i, "")
        return i

    def _skip_comment_run(self, toks, i):
        while i < len(toks) and toks[i]["k"] in ("doc", "comment", "nl"):
            # 空行断开
            if toks[i]["k"] == "nl":
                if i + 1 < len(toks) and toks[i + 1]["k"] == "nl":
                    return i + 1
            i += 1
        return i

    def _walk_decl(self, toks, fname, i, stack, file_ns, type_ctx, end_i, docs):
        """处理一个声明：using / namespace / 类型 / 成员。返回继续扫描的位置。"""
        tk = toks[i]
        t = tk["t"]
        if t == "using":
            i = skip_to_semicolon(toks, i)
            return i
        if t == "namespace":
            return self._parse_namespace(toks, fname, i, stack, file_ns, type_ctx, end_i, docs)
        # 属性
        attrs = ""
        if t == "[":
            attrs, i = parse_attrs(toks, i)
            if i >= end_i or toks[i]["k"] == "eof":
                return i
            tk = toks[i]
            t = tk["t"]
        # 修饰符（partial 是上下文关键字，以 ident 出现）
        mods = []
        while toks[i]["k"] not in ("eof",) and (
            (toks[i]["k"] == "kw" and toks[i]["t"] in MODIFIERS)
            or (toks[i]["k"] == "ident" and toks[i]["t"] in ("partial",))
        ):
            mods.append(toks[i]["t"])
            i += 1
            while i < end_i and toks[i]["k"] == "nl":
                i += 1
            if i >= end_i:
                break
        # 类型声明？
        if i < end_i and toks[i]["t"] in TYPE_DECL_KW or (
            i < end_i and toks[i]["k"] == "ident" and toks[i]["t"] == "record"
        ):
            if type_ctx is not None:
                # 嵌套类型 —— 作为封装类型的成员处理
                return self._parse_nested_type(toks, fname, i, stack, file_ns, type_ctx, end_i, docs, attrs, mods)
            return self._parse_type(toks, fname, i, stack, file_ns, end_i, docs, attrs, mods, type_ctx, type_ctx is not None)
        # 其余：类型体内的成员
        if type_ctx is not None:
            return self._parse_member(toks, fname, i, stack, file_ns, type_ctx, end_i, docs, attrs, mods)
        # 顶层杂项（如顶层 delegate）
        if i < end_i and toks[i]["t"] == "delegate":
            # 顶层 delegate 声明
            j = i
            while j < end_i and toks[j]["t"] not in (";", "{", "}") and toks[j]["k"] != "eof":
                j += 1
            if j < end_i and toks[j]["t"] == ";":
                sig = combine(toks[i:j + 1])
                self.add_type(self.ns_of(stack, file_ns), {
                    "kind": "delegate", "name": "", "tparams": "",
                    "bases": [], "attrs": attrs, "comment": docs,
                    "members": [], "files": [fname], "sig": clean_sig(sig),
                    "mods": " ".join(mods),
                }, fname)
                return j + 1
        # 未识别的顶层内容：跳过一行
        j = i
        while j < end_i and toks[j]["t"] not in (";", "{", "}") and toks[j]["k"] != "eof":
            j += 1
        if j < end_i and toks[j]["t"] in (";", "{"):
            if toks[j]["t"] == "{" and toks[j - 1]["t"] != "{":
                # 一个普通代码块 —— 整个吞掉
                j = find_matching(toks, j)
            else:
                j += 1
        return j

    def _parse_namespace(self, toks, fname, i, stack, file_ns, type_ctx, end_i, docs):
        i += 1
        parts = []
        while i < end_i and toks[i]["t"] != ";" and toks[i]["t"] != "{":
            if toks[i]["k"] in ("ident", "kw"):
                parts.append(toks[i]["t"])
            i += 1
        if i < end_i and toks[i]["t"] == ";":
            if type_ctx is None and not stack:
                self.file_ns = ".".join(parts)
            return i + 1
        if i < end_i and toks[i]["t"] == "{":
            body_end = find_matching(toks, i)
            sub = stack + ([".".join(parts)] if parts else [])
            # 块作用域：在 body 内部扫描
            self._walk(toks, fname, i + 1, sub, file_ns, None, body_end)
            # 但这里 file_ns 不适用……改为把子扫描交给 _walk 并返回
            return body_end
        return i + 1

    def _parse_type(self, toks, fname, i, stack, file_ns, end_i, docs, attrs, mods, type_ctx, nested=False):
        header = parse_type_header(toks, i)
        if not header:
            return i + 1
        kind = header["kind"]
        name = header["name"]
        j = header["i"]
        if kind == "delegate":
            # delegate R Name(args);
            while j < end_i and toks[j]["t"] not in (";", "{") and toks[j]["k"] != "eof":
                j += 1
            sig = combine(toks[i:j + 1]) if j < end_i and toks[j]["t"] == ";" else combine(toks[i:j])
            self.add_type(self.ns_of(stack, file_ns), {
                "kind": "delegate", "name": name, "tparams": header["tparams"],
                "bases": [], "attrs": attrs, "comment": docs, "members": [],
                "files": [fname], "sig": clean_sig(sig),
                "mods": " ".join(mods),
            }, fname)
            return j + 1 if j < end_i else j
        if kind == "enum":
            # 吞掉 { ... } 内的成员名
            names = []
            if j < end_i and toks[j]["t"] == "{":
                k = j + 1
                while k < end_i and toks[k]["t"] != "}":
                    if toks[k]["k"] in ("ident", "kw"):
                        en = toks[k]["t"]
                        k += 1
                        val = ""
                        while k < end_i and toks[k]["t"] not in (",", "}", "\n"):
                            if toks[k]["k"] not in ("nl",):
                                val += toks[k]["t"]
                            k += 1
                        if toks[k]["k"] == "nl":
                            pass
                        names.append((en, val.strip()))
                    elif toks[k]["k"] in ("nl", "comment"):
                        k += 1
                    else:
                        k += 1
                j = k
                # j 现在指向 '}' 处；若 '}' 后有 ';' 吞掉
                if j < end_i and toks[j]["t"] == "}":
                    j += 1
                    if j < end_i and toks[j]["t"] == ";":
                        j += 1
                elif j < end_i and toks[j]["t"] == "}":
                    pass
            self.add_type(self.ns_of(stack, file_ns), {
                "kind": "enum", "name": name, "tparams": header["tparams"],
                "bases": header["bases"], "attrs": attrs, "comment": docs,
                "members": [{"cat": "enum", "name": n, "sig": (n + ((" = " + v) if v else "")),
                             "comment": "", "vis": "public", "static": True, "file": fname}
                            for n, v in names],
                "files": [fname], "mods": " ".join(mods),
            }, fname)
            return j
        # class / struct / interface / record
        if kind == "record":
            # record Name(params); 或 record Name(params) { ... }
            sig = combine(toks[i:j])
            # 参数转成成员
            params = []
            if j < end_i and toks[j]["t"] == "(":
                k = j
                # 找配对 ')'，找 ';' 或 '{'
                depth = 0
                while k < end_i and toks[k]["k"] != "eof":
                    t2 = toks[k]["t"]
                    if t2 == "(":
                        depth += 1
                    elif t2 == ")":
                        depth -= 1
                        if depth == 0:
                            k += 1
                            break
                    k += 1
                param_txt = combine(toks[j:k])
                for p in param_txt.strip().strip("()").split(","):
                    p = p.strip()
                    if not p:
                        continue
                    m = re.match(r"^([\w\[\]<>?]+)\s+(\w+)", p)
                    if m:
                        params.append({"cat": "field", "name": m.group(2),
                                       "sig": m.group(1) + " " + m.group(2),
                                       "comment": "", "vis": "public", "static": False, "file": fname})
                    else:
                        params.append({"cat": "field", "name": p, "sig": p,
                                       "comment": "", "vis": "public", "static": False, "file": fname})
                j = k
            t = {
                "kind": "class", "record": True, "name": name,
                "tparams": header["tparams"], "bases": header["bases"],
                "attrs": attrs, "comment": docs, "members": params,
                "files": [fname], "sig": clean_sig(sig), "mods": " ".join(mods),
            }
            if j < end_i and toks[j]["t"] == "{":
                body_end = find_matching(toks, j)
                self._parse_members_into(toks, fname, j + 1, body_end, t)
                j = body_end
                if j < end_i and toks[j]["t"] == ";":
                    j += 1
            else:
                while j < end_i and toks[j]["t"] != ";" and toks[j]["k"] != "eof":
                    j += 1
                if j < end_i:
                    j += 1
            self.add_type(self.ns_of(stack, file_ns), t, fname)
            return j
        t = {
            "kind": kind, "name": name, "tparams": header["tparams"],
            "bases": header["bases"], "attrs": attrs, "comment": docs,
            "members": [], "files": [fname], "mods": " ".join(mods),
        }
        if j < end_i and toks[j]["t"] == "{":
            body_end = find_matching(toks, j)
            self._parse_members_into(toks, fname, j + 1, body_end, t)
            j = body_end
            if j < end_i and toks[j]["t"] == ";":
                j += 1
        self.add_type(self.ns_of(stack, file_ns), t, fname)
        return j

    def _parse_nested_type(self, toks, fname, i, stack, file_ns, type_ctx, end_i, docs, attrs, mods):
        # 用与顶层类型相同的方式解析，但其成员进入封闭类型
        header = parse_type_header(toks, i)
        if not header:
            return i + 1
        kind = header["kind"]
        name = header["name"]
        j = header["i"]
        # 找到主体结束，收集为嵌套类型成员
        if j < end_i and toks[j]["t"] == "{":
            body_end = find_matching(toks, j)
            inner = {
                "kind": kind, "name": name, "tparams": header["tparams"],
                "bases": header["bases"], "attrs": attrs, "comment": docs,
                "members": [], "files": [fname], "mods": " ".join(mods),
                "nested": True,
            }
            self._parse_members_into(toks, fname, j + 1, body_end, inner)
            type_ctx["members"].append({
                "cat": "type", "name": name, "sig": clean_sig(combine(toks[i:j])),
                "comment": docs, "vis": "public" if "public" in mods else "",
                "static": "static" in mods, "file": fname, "type": inner,
            })
            return body_end
        # 无体的嵌套声明（如枚举无体不太可能）
        type_ctx["members"].append({
            "cat": "type", "name": name, "sig": clean_sig(combine(toks[i:j])),
            "comment": docs, "vis": "public" if "public" in mods else "",
            "static": "static" in mods, "file": fname, "type": None,
        })
        return j

    def _parse_members_into(self, toks, fname, start, end, t):
        i = start
        guard = 0
        while i < end:
            guard += 1
            if guard > (end - start) * 20 + 2000:
                raise RuntimeError(f"members guard @{i} line {toks[i].get('line')} file={fname} type={t['name']}")
            tk = toks[i]
            if tk["k"] in ("nl", "comment", "directive"):
                i += 1
                continue
            if tk["t"] == "}":
                return
            docs = ""
            if tk["k"] == "doc":
                docs = doc_comment(toks, i)
                i = self._skip_comment_run(toks, i)
                # 跳过可能把"声明前空行吃掉的尾部换行"
                if i >= end or toks[i]["k"] == "eof":
                    break
            # 嵌套类型、属性、成员
            if toks[i]["t"] in TYPE_DECL_KW or (
                toks[i]["k"] == "ident" and toks[i]["t"] == "record"
            ):
                # 成员扫描前先检查修饰符结束后的类型声明
                j = i
                mm = []
                while j < end and (
                    (toks[j]["k"] == "kw" and toks[j]["t"] in MODIFIERS)
                    or (toks[j]["k"] == "ident" and toks[j]["t"] in ("partial",))
                ):
                    mm.append(toks[j]["t"])
                    j += 1
                    while j < end and toks[j]["k"] == "nl":
                        j += 1
                if j < end and (toks[j]["t"] in TYPE_DECL_KW or (
                    toks[j]["k"] == "ident" and toks[j]["t"] == "record"
                )):
                    i = self._parse_nested_type(toks, fname, j, [], None, t, end, docs, "", mm)
                    continue
                i = j
            m_end, member_toks, body_open = self._scan_member(toks, i, end, t)
            # body_open 是全局 token 索引，转成切片内索引
            body_local = (body_open - i) if body_open is not None else None
            self._classify_member(member_toks, t, fname, docs, body_local)
            i = m_end

    def _scan_member(self, toks, i, end, t):
        """从 toks[i] 扫描一个成员，返回 (结束索引, 成员 token 列表, 函数体 '{' 索引或 None)。"""
        start = i
        depth = 0
        paren = 0
        bracket = 0
        seen_eq = False
        body_open = None
        while i < end:
            tk = toks[i]
            if tk["k"] in ("eof",):
                break
            if tk["k"] in ("nl", "comment"):
                i += 1
                continue
            tt = tk["t"]
            if tt == "(":
                paren += 1
            elif tt == ")":
                paren -= 1
            elif tt == "[":
                bracket += 1
            elif tt == "]":
                bracket -= 1
            elif tt == "=" and not seen_eq and depth == 0:
                seen_eq = True
            elif tt == "{" or tt == "}":
                if tt == "{":
                    if depth == 0 and paren == 0 and bracket == 0 and not seen_eq:
                        body_open = i
                        depth = 1
                    else:
                        depth += 1
                else:
                    if depth == 0:
                        # 类型体结束（是类型的结束而非成员）
                        break
                    depth -= 1
                    if depth == 0 and body_open is not None:
                        i += 1
                        break
            elif tt == ";":
                if depth == 0 and paren == 0 and bracket == 0:
                    i += 1
                    break
            i += 1
        return i, toks[start:i], body_open

    def _classify_member(self, member_toks, t, fname, docs, body_open):
        if not member_toks:
            return
        # 有函数体：属性保留访问器名（get/set/init），其余裁剪函数体。
        if body_open is not None:
            bi = body_open + 1
            while bi < len(member_toks) and member_toks[bi]["k"] in ("nl", "comment"):
                bi += 1
            is_prop = bi < len(member_toks) and member_toks[bi]["t"] in ("get", "set", "init")
            if is_prop:
                acc = []
                for tk in member_toks[body_open:]:
                    if tk["t"] in ("get", "set", "init"):
                        acc.append(tk["t"])
                member_toks = member_toks[:body_open] + [
                    {"k": "punct", "t": "{", "line": 0},
                    {"k": "ident", "t": " ".join(dict.fromkeys(acc)), "line": 0},
                    {"k": "punct", "t": "}", "line": 0},
                ]
            else:
                member_toks = member_toks[:body_open]
        header = combine(member_toks)
        header = clean_sig(header)
        # 头部解析：属性 + 修饰符
        mods = []
        attrs = ""
        j = 0
        # 从 token 中找属性与修饰符
        parts = []
        for tk in member_toks:
            if tk["k"] in ("nl", "comment"):
                continue
            parts.append(tk)
        # 用 token 级别判断更稳
        base = []
        attrs_tk = []
        idx = 0
        while idx < len(parts):
            tk = parts[idx]
            if tk["t"] == "[":
                a, idx2 = parse_attrs(parts, idx)
                attrs_tk.append(a)
                idx = idx2
                continue
            if tk["k"] == "kw" and tk["t"] in MODIFIERS:
                mods.append(tk["t"])
                idx += 1
                if tk["t"] == "const":
                    mods.append("static")
                continue
            if tk["t"] in (";",):
                break
            if tk["t"] == "(":
                break
            base = parts[idx:]
            break
        if not base:
            return
        # 属性文本
        attrs = ", ".join(attrs_tk)
        # 基础类别判定
        first = base[0]
        rec = {
            "vis": "public",
            "static": "static" in mods,
            "async": "async" in mods,
            "attrs": attrs,
            "comment": docs,
            "file": fname,
        }
        if "private" in mods:
            rec["vis"] = "private"
        elif "protected" in mods:
            rec["vis"] = "protected"
        elif "internal" in mods:
            rec["vis"] = "internal"
        text0 = combine(base)
        whole = combine(parts)
        whole = clean_sig(whole)
        # 1) 析构函数
        if first["t"] == "~":
            rec.update({"cat": "destructor", "name": "", "sig": whole})
            t["members"].append(rec)
            return
        # 2) 嵌套类型
        if first["t"] in TYPE_DECL_KW or (first["k"] == "ident" and first["t"] == "record"):
            self._nested_from_member(parts, t, fname, docs, mods, attrs)
            return
        # 3) operator
        if first["t"] == "operator" or (first["k"] == "kw" and first["t"] == "operator"):
            rec.update({"cat": "operator", "name": "_op", "sig": whole})
            t["members"].append(rec)
            return
        # 4) event
        if first["t"] == "event":
            m = re.match(r"event\s+([\w\[\]<>?,.\s]+?)\s+(\w+)\s*;", whole)
            if m:
                rec.update({"cat": "event", "name": m.group(2), "sig": "event " + m.group(1).strip() + " " + m.group(2), "type": m.group(1).strip()})
            else:
                rec.update({"cat": "event", "name": "", "sig": whole})
            t["members"].append(rec)
            return
        # 5) 构造 / 方法 / 索引器 / 属性 / 字段
        # 找出“名字 token”: 基础序列中第一个后跟 ( 或 [ 或 { 或 ; 的标识符
        # 更直接：找 token 序列中的模式
        #   - 若有 (：方法/构造/operator
        #   - 若有 { get/set/init } 或 -> ：属性/索引器
        #   - 否则字段
        has_fn = any(x["t"] == "(" for x in base)
        has_arr_eq = any(x["t"] == "=" for x in base)
        has_block = any(x["t"] == "{" for x in base)
        has_arrow = any(x["t"] == "=>" for x in base)
        # 属性：{ get … } / { set … } / { init … }
        prop_kind = None
        if has_block and not has_arr_eq:
            for x in base:
                if x["t"] in ("get", "set", "init"):
                    prop_kind = x["t"]
                    break
        if prop_kind:
            # this[...] 索引器？
            if base[0]["t"] == "this":
                rec.update({"cat": "indexer", "name": "this", "sig": whole})
                t["members"].append(rec)
                return
            # 属性：type Name { … }
            m = re.match(r"^([\w\[\]<>?,.\s]+?)\s+(\w+)\s*\{", whole)
            if m and m.group(2) not in MODIFIERS:
                acc = [x["t"] for x in base if x["t"] in ("get", "set", "init")]
                rec.update({"cat": "property", "name": m.group(2),
                            "sig": m.group(1).strip() + " " + m.group(2) + " { " + ", ".join(dict.fromkeys(acc)) + " }",
                            "type": m.group(1).strip()})
            else:
                rec.update({"cat": "property", "name": "", "sig": whole})
            t["members"].append(rec)
            return
        if has_fn:
            # 方法 / 构造
            # 找出 '(' 位置前最后两个 token：<name> (
            pidx = next((k for k, x in enumerate(base) if x["t"] == "("), None)
            name_tok = base[pidx - 1]["t"] if pidx and pidx > 0 else ""
            # 构造：name == 类型名
            if name_tok == t["name"]:
                rec.update({"cat": "constructor", "name": name_tok, "sig": whole})
                t["members"].append(rec)
                return
            if name_tok == "":
                rec.update({"cat": "method", "name": "", "sig": whole})
                t["members"].append(rec)
                return
            # 提取返回类型
            ret_txt = combine(base[:pidx - 1])
            rec.update({"cat": "method", "name": name_tok, "sig": whole, "ret": ret_txt})
            t["members"].append(rec)
            return
        # 索引器 this[int i]
        if base[0]["t"] == "this":
            rec.update({"cat": "indexer", "name": "this[]", "sig": whole})
            t["members"].append(rec)
            return
        # 显式/隐式转换 operator - 已在 3 处理
        # 字段
        m = re.match(r"^([\w\[\]<>?.,\s]+?)\s+(\w+)\s*(=.*)?$", whole)
        if m and m.group(2) not in MODIFIERS and not m.group(2).startswith("_op"):
            rec.update({"cat": "field", "name": m.group(2), "type": m.group(1).strip(),
                        "sig": whole})
        else:
            rec.update({"cat": "field", "name": "", "sig": whole})
        t["members"].append(rec)

    def _nested_from_member(self, parts, t, fname, docs, mods, attrs):
        # 已经确定是嵌套类型 —— 用与 parse_type 相同的逻辑
        # 找到类型关键字
        ki = next((k for k, x in enumerate(parts) if x["t"] in TYPE_DECL_KW), None)
        if ki is None:
            return
        header = parse_type_header(parts, ki)
        if not header:
            return
        kind = header["kind"]
        name = header["name"]
        j = header["i"]
        inner = {
            "kind": kind, "name": name, "tparams": header["tparams"],
            "bases": header["bases"], "attrs": attrs, "comment": docs,
            "members": [], "files": [fname], "mods": " ".join(mods), "nested": True,
        }
        # parts 是 member 的切片；j 是 parts 内索引 —— 需要体
        if j < len(parts) and parts[j]["t"] == "{":
            # 从整个 token 流中找体 —— 重新用原始流不可得；这里从 parts 之后
            # 直接构造
            pass
        t["members"].append({
            "cat": "type", "name": name, "sig": clean_sig(combine(parts)),
            "comment": docs, "vis": "public" if "public" in mods else "",
            "static": "static" in mods, "file": fname, "type": inner,
        })

    def run(self, stdlib):
        self.stdlib = stdlib
        self.file_ns = ""
        files = []
        for root, _dirs, names in os.walk(stdlib):
            for nm in sorted(names):
                if nm.endswith(".zan"):
                    files.append(os.path.join(root, nm))
        for path in sorted(files):
            self.file_ns = ""
            try:
                self.parse_file(path)
            except Exception as e:  # 单文件失败不中断
                print(f"WARN parse {path}: {e}", file=sys.stderr)
        # 排序输出
        data = {}
        for ns in self.order:
            d = self.namespaces[ns]
            d["types"].sort(key=lambda x: (x["kind"] != "class", x["kind"], x["name"]))
            data[ns] = d
        return data


def main():
    stdlib = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "stdlib")
    outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "public", "ref")
    ex = Extractor()
    data = ex.run(os.path.normpath(stdlib))
    os.makedirs(outdir, exist_ok=True)
    # 全量 API 模型是构建中间产物，只放 gen/（不要部署）；
    # index.json（命名空间→类型清单）是轻量索引，放 public/ref 供 AI 检索。
    gen_dir = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(gen_dir, "ref-data.json"), "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    idx = {}
    for ns, d in data.items():
        idx[ns] = {
            "files": d["files"],
            "types": [{ "kind": x["kind"], "name": x["name"], "tparams": x.get("tparams", "")}
                      for x in d["types"]],
        }
    with open(os.path.join(outdir, "index.json"), "w", encoding="utf-8") as f:
        json.dump(idx, f, ensure_ascii=False, indent=1)
    print(f"OK: {len(data)} namespaces, "
          f"{sum(len(d['types']) for d in data.values())} types -> {outdir}")


if __name__ == "__main__":
    main()
