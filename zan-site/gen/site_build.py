#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
site_build.py — zan-site 文档构建器。

1. 把 guides/*.md（手写指南）渲染成 public/<name>.html + public/<name>.md；
2. 把 public/ref/data.json（gen/api_extract.py 从 stdlib 提取的 API）渲染成
   public/ref/<ns>.html 与 public/ref/<ns>.md；
3. 生成 /ref 的层级总览。

用法: python gen/site_build.py [--ref-only]
"""
import html
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUIDES = os.path.join(ROOT, "guides")
PUB = os.path.join(ROOT, "public")
REF = os.path.join(PUB, "ref")
DIST = os.path.join(ROOT, ".sitebuild")  # 构建产物暂存（最终拷到 public）

NAV = [
    ("/", "首页", "Home"),
    ("/wiki", "IDE 指南", "IDE guide"),
    ("/lang", "语言参考", "Language"),
    ("/gui", "GUI 指南", "Gui guide"),
    ("/stdlib", "标准库", "Stdlib"),
    ("/examples", "示例", "Examples"),
    ("/ai-connect", "AI 接入", "AI connect"),
]
DOWNLOAD_URL = "https://baokuan.lanzoue.com/b004jl514b"


# --------------------------------------------------------------------- #
# Markdown → HTML（覆盖文档所需子集）
# --------------------------------------------------------------------- #
def md_inline(text):
    """行内解析：`code`、**bold**、*italic*、[link](url)、自动保留 < >。"""
    # code span 先占位
    codes = []
    def stash(m):
        codes.append(m.group(1))
        return "\x00%d\x00" % (len(codes) - 1)
    text = re.sub(r"`([^`]+)`", lambda m: stash(m), text)

    def strip_escape(s):
        return s.replace("\\*", "*").replace("\\`", "`").replace("\\[", "[")

    text = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<i>\1</i>", text)
    text = re.sub(
        r"\[([^\]]+)\]\(([^)\s]+)\)",
        lambda m: f'<a href="{m.group(2)}">{m.group(1)}</a>', text)

    def restore(m):
        i = int(m.group(1))
        code = html.escape(codes[i])
        if code.startswith("|") or code.startswith("~"):
            return "<code class=\"inline\">" + code + "</code>"
        return "<code class=\"inline\">" + code + "</code>"
    text = re.sub(r"\x00(\d+)\x00", restore, text)
    return text


def slugify(text):
    s = re.sub(r"[^\w\u4e00-\u9fff\- ]", "", text.strip().lower())
    s = re.sub(r"\s+", "-", s)
    return s or "sec"


def md_blocks(text):
    """把 markdown 切成 (kind, payload) 块。"""
    lines = text.split("\n")
    blocks = []
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        if line.startswith("```"):
            lang = line[3:].strip()
            buf = []
            i += 1
            while i < n and not lines[i].startswith("```"):
                buf.append(lines[i])
                i += 1
            i += 1
            blocks.append(("code", (lang, "\n".join(buf))))
            continue
        if line.startswith("|"):
            buf = []
            while i < n and lines[i].startswith("|"):
                buf.append(lines[i])
                i += 1
            blocks.append(("table", buf))
            continue
        m = re.match(r"^(#{1,6})\s+(.*)$", line)
        if m:
            blocks.append(("h%d" % len(m.group(1)), m.group(2)))
            i += 1
            continue
        if re.match(r"^\s*(---+|\*\*\*+)\s*$", line):
            blocks.append(("hr", ""))
            i += 1
            continue
        if line.startswith(">"):
            buf = []
            while i < n and lines[i].startswith(">"):
                buf.append(re.sub(r"^>\s?", "", lines[i]))
                i += 1
            blocks.append(("quote", "\n".join(buf)))
            continue
        m = re.match(r"^\s*([-*])\s+(.*)$", line)
        if m:
            buf = []
            while i < n:
                m2 = re.match(r"^\s*([-*])\s+(.*)$", lines[i])
                if m2:
                    buf.append(m2.group(2))
                    i += 1
                else:
                    break
            blocks.append(("ul", buf))
            continue
        m = re.match(r"^\s*(\d+)\.\s+(.*)$", line)
        if m:
            buf = []
            while i < n:
                m2 = re.match(r"^\s*\d+\.\s+(.*)$", lines[i])
                if m2:
                    buf.append(m2.group(1))
                    i += 1
                else:
                    break
            blocks.append(("ol", buf))
            continue
        if line.strip() == "":
            i += 1
            continue
        buf = []
        while i < n and lines[i].strip() != "" and not (
            lines[i].startswith("|") or lines[i].startswith("```")
            or re.match(r"^\s*[-*]\s+", lines[i])
            or re.match(r"^\s*\d+\.\s+", lines[i])
            or re.match(r"^#{1,6}\s+", lines[i])
            or lines[i].startswith(">")):
            # 若这是代码块前的普通文本行则停止
            buf.append(lines[i])
            i += 1
        # 合并前一行尾随空格换行
        blocks.append(("p", "\n".join(buf)))
    return blocks


def md_render(text):
    out = []
    for kind, body in md_blocks(text):
        if kind == "code":
            lang, code = body
            extra = " prompt" if lang == "prompt" else ""
            out.append('<div class="code-card%s"><div class="code-bar"><i></i><i></i><i></i><span>%s</span></div>\n<pre><code>%s</code></pre></div>'
                       % (extra, html.escape(lang or "zan"), html.escape(code)))
        elif kind == "table":
            rows = []
            for r in body:
                # 表格单元格内的 \| 是转义的字面 |（如运算符表），先占位再还原
                r = r.replace("\\|", "\x01")
                cells = [c.strip().replace("\x01", "|") for c in r.strip().strip("|").split("|")]
                rows.append(cells)
            if not rows:
                continue
            thead = rows[0]
            tbody = rows[1:]
            h = "<table><thead><tr>" + "".join(f"<th>{md_inline(c)}</th>" for c in thead) + "</tr></thead><tbody>"
            for r in tbody:
                h += "<tr>" + "".join(f"<td>{md_inline(c)}</td>" for c in r) + "</tr>"
            h += "</tbody></table>"
            out.append(h)
        elif kind == "hr":
            out.append("<hr/>")
        elif kind.startswith("h"):
            lv = int(kind[1:])
            txt = body
            out.append(f"<h{lv} id=\"{slugify(txt)}\">{md_inline(txt)}</h{lv}>")
        elif kind == "quote":
            out.append("<blockquote>" + md_render("\n\n".join(body.split("\n"))) + "</blockquote>")
        elif kind == "ul":
            out.append("<ul>" + "".join(f"<li>{md_inline(x)}</li>" for x in body) + "</ul>")
        elif kind == "ol":
            out.append("<ol>" + "".join(f"<li>{md_inline(x)}</li>" for x in body) + "</ol>")
        elif kind == "p":
            out.append("<p>" + md_inline(body) + "</p>")
    return "\n".join(out)


# --------------------------------------------------------------------- #
# 页面模板
# --------------------------------------------------------------------- #
def page(title, desc, hero_kicker, hero_title, hero_lede, active, sidebar_html, content_html, full_width=False):
    tabs = []
    for href, zh, en in NAV:
        cls = "active" if href == active else ""
        tabs.append(f'<a href="{href}" class="{cls}"><span class="zh">{zh}</span><span class="en">{en}</span></a>')
    navlinks = []
    for href, zh, en in NAV:
        cls = "active" if href == active else ""
        navlinks.append(f'<a href="{href}" class="{cls}"><span class="zh">{zh}</span><span class="en">{en}</span></a>')
    # full_width 页面去掉左侧空侧栏，正文占满整宽（导航/页脚/doc-tabs 保留）
    cls = " wiki-full" if full_width else ""
    if full_width:
        sidebar_block = ""
    else:
        sidebar_block = (
            '    <aside class="wiki-side doc-side">\n'
            '      <div class="searchbox"><svg class="ic" style="width:16px;height:16px" viewBox="0 0 24 24" fill="none" stroke="var(--muted)" stroke-width="2"><circle cx="11" cy="11" r="7"/><path d="m21 21-4.3-4.3"/></svg>\n'
            '        <input id="docSearch" placeholder="筛选目录…" aria-label="filter"></div>\n'
            f'      {sidebar_html}\n'
            '    </aside>'
        )
    return f"""<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(title)} · Zan</title>
<meta name="description" content="{html.escape(desc)}">
<link rel="icon" href="/favicon.svg">
<link rel="stylesheet" href="/styles.css">
</head>
<body class="lang-zh">
<svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>
  <linearGradient id="ig" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#5b6cff"/><stop offset="1" stop-color="#9b5cff"/>
  </linearGradient></defs></svg>

<header class="nav">
  <div class="wrap nav-inner">
    <a class="brand" href="/"><img class="brand-mascot" src="/mascot.png" alt=""/><span>Zan</span></a>
    <nav class="nav-links">
      {"".join(navlinks)}
      <button class="lang-btn" id="langBtn">EN</button>
      <a data-download href="{DOWNLOAD_URL}"><span class="zh">下载</span><span class="en">Download</span></a>
    </nav>
  </div>
</header>

<main>
  <section class="doc-hero">
    <div class="glow b"></div>
    <div class="wrap">
      <img class="hero-mascot" src="/mascot.png" alt=""/>
      <p class="kicker" style="text-align:left">{hero_kicker}</p>
      <h1 class="grad">{hero_title}</h1>
      <p class="lede">{hero_lede}</p>
      <p style="font-size:13.5px;color:var(--muted);margin:6px 0 0">
        <span class="zh">指南正文以中文编写；导航与界面文案提供英文切换。英文原文请抓取对应 .md 或见仓库 docs/。</span>
        <span class="en">This guide is written in Chinese; the navigation Chrome switches to English. Fetch the .md copy for the raw text.</span>
      </p>
      <div class="doc-tabs">
        {"".join(tabs)}
      </div>
    </div>
  </section>

  <div class="wrap wiki{cls}">
{sidebar_block}
    <div class="wiki-main">
      {content_html}
    </div>
  </div>
</main>

<footer class="footer">
  <div class="wrap footer-inner">
    <div class="brand"><img class="brand-mascot" src="/mascot.png" alt=""/><span>Zan</span></div>
    <p><span class="zh">用 C# 语法做轻量、高性能、漂亮的跨平台原生应用。</span>
      <span class="en">Lightweight, high-performance, beautiful cross-platform native apps in C#-style syntax.</span></p>
    <div class="footer-links">
      {"".join(f'<a href="{h}"><span class="zh">{z}</span><span class="en">{e}</span></a>' for h, z, e in NAV)}
    </div>
    <p class="copyright">© 2026 Zan Language · MIT</p>
  </div>
</footer>

<div class="modal" id="dlModal" role="dialog" aria-modal="true">
  <div class="modal-card">
    <button class="modal-x" aria-label="close">&times;</button>
    <img class="m-mascot" src="/mascot.png" alt=""/>
    <h3><span class="zh">下载 Zan IDE</span><span class="en">Download Zan IDE</span></h3>
    <p><span class="zh">网盘下载需要提取码，请先复制：</span><span class="en">The download needs an access code — copy it first:</span></p>
    <div class="code-box">
      <span class="code-val">zanlang</span>
      <button class="copy-btn" data-copy="zanlang"><span class="zh">复制</span><span class="en">Copy</span></button>
    </div>
    <p class="modal-hint"><span class="zh">蓝奏云<b>不能自动填充密码</b>，打开后请在页面手动粘贴提取码。</span><span class="en">The netdisk <b>can't auto-fill</b> — paste the code manually on its page.</span></p>
    <a class="btn btn-primary" href="{DOWNLOAD_URL}" target="_blank" rel="noopener"><span class="zh">前往下载页</span><span class="en">Go to download page</span></a>
  </div>
</div>

<script src="/app.js"></script>
</body>
</html>
"""


def sidebar_toc(md_text):
    """从 markdown 提取标题生成侧边栏 TOC：h2 一级可点链接，h3 缩进二级。"""
    out = ["<div data-grp>"]
    for kind, body in md_blocks(md_text):
        if kind == "h2":
            out.append(f'<a href="#{slugify(body)}" class="lvl1">{html.escape(body)}</a>')
        elif kind == "h3":
            out.append(f'<a href="#{slugify(body)}" class="lvl2">{html.escape(body)}</a>')
    out.append("</div>")
    return "\n".join(out)


# --------------------------------------------------------------------- #
# 指南页
# --------------------------------------------------------------------- #
def md_to_markdown_out(text):
    """原样输出 markdown（供 AI 检索）。"""
    return text


def build_guides():
    meta = {
        "lang": {
            "desc": "Zan 语言完整参考：词法、类型系统、声明、语句、运算符、泛型、异步、ARC 内存、FFI、属性、预处理与工具链。",
        },
        "gui": {
            "desc": "Zan GUI 开发指南：App/Form/Control 架构、.zform 设计文档格式、代码式组件操作、布局、样式/主题/皮肤、事件、控件大全与真实示例。",
        },
        "wiki": {
            "desc": "Zan IDE 指南：下载安装、项目模板、编辑器、可视化 .zform 设计器、运行/调试、发布、AI 助手与 MCP。",
        },
        "stdlib": {
            "desc": "Zan 标准库总览与 API 参考：按命名空间索引，覆盖 System/Gui/Game/Sdk/SDL3，提供完整签名与说明。",
        },
        "examples": {
            "desc": "Zan 示例与项目模板：console/gui/server-mvc/server-iot/server-http/server-ws-gateway/server-tcp/library 等可运行模板与原样示例。",
        },
    }
    for name in os.listdir(GUIDES):
        if not name.endswith(".md"):
            continue
        page_name = name[:-3]
        with open(os.path.join(GUIDES, name), encoding="utf-8") as f:
            text = f.read()
        # 标题从第一个 #
        m = re.search(r"^#\s+(.+)$", text, re.M)
        hero_title = m.group(1) if m else page_name
        desc = meta.get(page_name, {}).get("desc", "")
        html_body = md_render(text)
        # 跳过首个 h1（复用为 hero）
        html_body = re.sub(rf'^<h1[^>]*>{re.escape(md_inline(hero_title))}</h1>\s*', "", html_body) if m else html_body
        content = html_body
        sb = sidebar_toc(text)
        base_title = re.sub(r"（Zan）", "", hero_title).strip()
        out = page(base_title, desc,
                   '<span class="zh">知识库 · 指南</span><span class="en">Knowledge base</span>',
                   hero_title,
                   f'<span class="zh">完整开发文档，可检索、可复制。</span><span class="en">Full development docs.</span>',
                   "/" + page_name, sb, content)
        with open(os.path.join(REF, "..", page_name + ".html"), "w", encoding="utf-8") as f:
            f.write(out)
        with open(os.path.join(PUB, page_name + ".md"), "w", encoding="utf-8") as f:
            f.write(text)
        print("guide:", page_name + ".html", len(text), "chars")


# --------------------------------------------------------------------- #
# 参考页
# --------------------------------------------------------------------- #
NS_GROUP = [
    ("System", "System"),
    ("Gui", "Gui"),
    ("Game", "Game"),
    ("Sdk", "Sdk"),
    ("SDL3", "SDL3"),
    ("Platform", "Platform"),
]

CAT_LABEL = {
    "field": "字段", "property": "属性", "constructor": "构造", "method": "方法",
    "operator": "运算符", "event": "事件", "destructor": "析构", "indexer": "索引器",
    "type": "嵌套类型", "enum": "枚举值",
}
CAT_ORDER = ["field", "property", "constructor", "method", "operator", "event", "indexer", "destructor", "type", "enum"]


def ref_sidebar(data, current):
    roots = {}
    for ns in sorted(data):
        root = ns.split(".")[0]
        roots.setdefault(root, []).append(ns)
    out = []
    for root in ["System", "Gui", "Game", "Sdk", "SDL3", "Platform"]:
        nss = roots.get(root, [])
        if not nss:
            continue
        # 组用 <details>：点击组标题可展开/收起（Sdk/Platform 默认收起）
        open_attr = "" if root in ("Sdk", "Platform") else " open"
        out.append(f'<details class="nsgrp"{open_attr}><summary>{root} · {len(nss)}</summary>')
        for ns in nss:
            cls = ' class="active"' if ns == current else ""
            out.append(f'<a href="/ref/{ns}"{cls}>{html.escape(ns)}</a>')
        out.append("</details>")
    return "\n".join(out)


def esc(s):
    return html.escape(s or "")


def doc_html(doc):
    """/// 文档注释 → HTML：反引号片段转为 <code>，其余转义为文本。"""
    d = esc((doc or "").strip())
    out = []
    open_code = False
    for ch in d:
        if ch == "`":
            out.append("<code>" if not open_code else "</code>")
            open_code = not open_code
        else:
            out.append(ch)
    return "".join(out)


def member_html(m, indent=""):
    vis = m.get("vis", "public")
    sig = esc(m.get("sig", ""))
    doc = doc_html(m.get("comment"))
    cls = "pv" if vis in ("private",) else ""
    parts = [f'<li class="{cls}"><code class="sig">{sig}</code>']
    if doc:
        parts.append(f'<div class="mdoc">{doc}</div>')
    parts.append("</li>")
    return "\n".join(parts)


def type_html(t):
    kind = t["kind"]
    mods = esc(t.get("mods", ""))
    attrs = esc(t.get("attrs", ""))
    sig = t.get("sig")
    if sig:
        head = f'<code class="sig">{esc(sig)}</code>'
    else:
        head = f'<span class="t">{esc(t["name"])}</span>'
    docs = (t.get("comment") or "").strip()
    tag = "record" if t.get("record") else kind
    out = [f'<div class="api"><div class="api-head"><span class="ns">{esc(t["name"])}</span><span class="tag">{tag}</span></div>']
    out.append('<div class="api-body">')
    if mods or attrs:
        out.append(f'<div class="mods">{esc(" ".join(x for x in (mods, attrs) if x))}</div>')
    if docs:
        # doc_html 内部会转义 + 把反引号转为 <code>
        out.append(f'<div class="mdoc">{doc_html(docs)}</div>')
    members = t.get("members", [])
    groups = {}
    for m in members:
        cat = m.get("cat", "method")
        groups.setdefault(CAT_LABEL.get(cat, cat), []).append(m)
    for cat in CAT_ORDER:
        label = CAT_LABEL.get(cat)
        if label not in groups:
            continue
        mlist = groups[label]
        pub = [m for m in mlist if m.get("vis") not in ("private",)]
        priv = [m for m in mlist if m.get("vis") == "private"]
        if pub:
            out.append(f'<h5 class="cat">{label}</h5><ul class="sig">')
            for m in pub:
                out.append(member_html(m))
            out.append("</ul>")
        if priv:
            out.append(f'<details class="priv"><summary>{label}（内部）</summary><ul class="sig">')
            for m in priv:
                out.append(member_html(m))
            out.append("</ul></details>")
    out.append("</div></div>")
    return "\n".join(out)


def ref_md_ns(ns, d):
    out = [f"# {ns}\n"]
    files = d.get("files", [])
    if files:
        out.append("> 源码: " + ", ".join(f"`stdlib/{f}`" for f in files) + "\n")
    for t in d.get("types", []):
        out.append(f"\n## {t['name']} ({t['kind']})\n")
        doc = (t.get("comment") or "").strip()
        if doc:
            out.append(doc + "\n")
        sig = t.get("sig")
        if sig:
            out.append(f"`{sig}`\n")
        for m in t.get("members", []):
            s = m.get("sig", "")
            c = (m.get("comment") or "").strip()
            out.append(f"- {s}")
            if c:
                out.append("  - " + c.replace("\n", "\n    "))
            out.append("")
    return "\n".join(out)


def build_ref():
    # 全量 API 模型是构建中间产物，位于 gen/ref-data.json（不部署）
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "ref-data.json"),
              encoding="utf-8") as f:
        data = json.load(f)
    # 清理旧产物（data.json/index.json 除外），保证重命名命名空间不残留旧页
    for fn in os.listdir(REF):
        if fn.endswith((".html", ".md")):
            os.remove(os.path.join(REF, fn))
    for ns in sorted(data):
        d = data[ns]
        # HTML
        content = []
        files = d.get("files", [])
        if files:
            content.append('<div class="note"><p><span class="zh">源码文件：</span>'
                           + ", ".join(f'<code class="inline">stdlib/{esc(f)}</code>' for f in files)
                           + "</p></div>")
        for t in d.get("types", []):
            content.append(type_html(t))
        out = page(ns + " — 标准库参考 · Zan",
                   ns + " 标准库 API 参考：完整类型与成员签名，来自 stdlib 源码。",
                   '<span class="zh">知识库 · 标准库参考</span><span class="en">API reference</span>',
                   ns,
                   f'<span class="zh">{len(d.get("types", []))} 个类型 · 完整签名由 stdlib 源码提取</span>'
                   f'<span class="en">{len(d.get("types", []))} types · extracted from stdlib source</span>',
                   "/stdlib.html", ref_sidebar(data, ns), "\n".join(content))
        with open(os.path.join(REF, ns + ".html"), "w", encoding="utf-8") as f:
            f.write(out)
        # Markdown
        with open(os.path.join(REF, ns + ".md"), "w", encoding="utf-8") as f:
            f.write(ref_md_ns(ns, d))
    # 总览页（stdlib.html 侧栏也需要，这里不做，交给 guides/stdlib.md）
    print("ref:", len(data), "namespaces")


# --------------------------------------------------------------------- #
# AI 接入页（/ai-connect）
# --------------------------------------------------------------------- #
def build_ai_mirror():
    """生成 /ai-connect：一行提示词，让 AI 自行配置并开始工作。

    刻意只放提示词，不镜像 AGENTS.md / skills / mcp 配置正文——那些内容在
    SDK 的 tools\ai_pack\ 里，也散落在本站 /wiki、/examples、/ref 页面上，
    在这里重复全文只会让页面又长又没人读。单一数据源意味着这里只做"入口"。
    """
    parts = []
    parts.append("# AI 接入（Zan 助手）\n")
    parts.append(
        "把这**一行提示词**发给你的 AI 编码工具（Claude Code / Cursor / Copilot / Windsurf / Devin…），"
        "它会自己配置并开始工作：\n")
    parts.append(
        "```prompt\n"
        "你是 Zan 项目的 AI 编码助手。先运行 `tools\\zan-mcp.exe --init-agent .` 安装项目配置，"
        "然后调用 `zan_start_here` 获取项目布局、入口、构建/测试命令、规则和工具目录，"
        "再按 `zan-development` 技能开始编码；不确定的 API 先用 `zan_api_search` 查证，改完编译。\n"
        "```\n")
    parts.append(
        "> SDK 不在当前目录就把 `tools\\zan-mcp.exe` 换成绝对路径；或在 IDE 助手面板一键启用。\n")

    mirror_md = "\n".join(parts)
    content = md_render(mirror_md)
    out = page("AI 接入 · Zan",
               "把 AI 编码工具接到 Zan：一行提示词，让 AI 自行配置并开始工作。",
               '<span class="zh">AI 接入</span><span class="en">AI connect</span>',
               "AI 接入（Zan 助手）",
               '<span class="zh">把这个提示词发给你的 AI 编码工具，它会自己配置并开始工作。</span>'
               '<span class="en">Give this prompt to your AI tool; it configures itself.</span>',
               "/ai-connect", "", content, full_width=True)
    with open(os.path.join(PUB, "ai-connect.html"), "w", encoding="utf-8") as f:
        f.write(out)
    with open(os.path.join(PUB, "ai-connect.md"), "w", encoding="utf-8") as f:
        f.write(mirror_md)
    print("ai mirror: /ai-connect.html + /ai-connect.md")


def main():
    only_ref = "--ref-only" in sys.argv
    if not only_ref:
        build_guides()
    build_ref()
    build_ai_mirror()
    print("done")


if __name__ == "__main__":
    main()
