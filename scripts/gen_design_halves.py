#!/usr/bin/env python3
"""B-ID44: 从 IDE 设计稿(.html)生成运行期投影半件(<Name>.design.zan)。

编译期 HTML 通道删除后，设计稿在运行期经 DesignedControl 投影。本脚本
扫 src/ide_zan/src 下每份 role:control 设计稿，产出声明 `partial class
<Name> : DesignedControl` 的半件：构造期 Design() 投影文档，命名控件
以 accessor 属性暴露（code-behind 的 this.Tree 等引用零改动）。

设计稿改动（增删命名控件、换 kind）后重跑本脚本即可；手工逻辑写在
code-behind（<Name>.zan），半件只承载投影声明。
"""
import os
import re
import glob
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src', 'ide_zan', 'src')

# kind → accessor 类型表之外的都按同名类解析（Gui.Widget/Gui.Component/
# IDE 全局类同名约定）。泛型容器按 data-of 落类型参数。
GENERIC = {'ListView': 'ListView<{of}>', 'DataGrid': 'DataGrid<{of}>'}

HEADER = """// <auto-design> {doc}.html 的运行期投影半件（B-ID44）——由
// scripts/gen_design_halves.py 生成，勿手改；逻辑写在 code-behind
// {doc}.zan。设计稿改动后重跑生成脚本。
"""

# 组件自注册约定（零清单扩展）：被任何文档引用的 kind，其同名类
# 要么是设计类（本脚本发工厂），要么在自己的类文件里声明
# `static void Install()`（注册表发一条调用）。两者都不满足的
# （内置控件、泛型容器）不进注册表——它们由包级 Bootstrap 覆盖。
SCAN_ROOTS = [SRC, os.path.join(ROOT, 'packages', 'Zan.Gui', 'src')]

REGISTRY = """// <auto-design> IDE 组件注册表（B-ID44）——由
// scripts/gen_design_halves.py 扫描全部 .html 设计稿的 kind 引用
// 生成：设计类发 HeavyControls 工厂；类文件声明了 static void
// Install() 的组件类发一条调用。新增组件只需新增文档（或带
// Install 的组件类）后重新构建，本表自动更新。勿手改。
using Gui;
using Gui.Widget;
using Gui.Component;

public static class IdeDesignedRegistry {{
    static bool installed;

    // 注册文档实际引用的全部 kind（幂等）。未注册的 kind 显式
    // 失败（节点为 null），拼错的名字不会静默变型。
    public static void Install() {{
        if (installed) {{ return; }}
        installed = true;
        Gui.ControlBootstrap.Install();
        Gui.Component.DataTable.DataTableBootstrap.Install();
{lines}
    }}
}}
"""


def tag_kind(tag, attrs):
    """与 DesignerHtml.TagKind 同表：裸容器兜底 Panel，zan-* 恢复 Pascal。"""
    t = tag.lower()
    if t == 'body':
        return None
    km = re.search(r'data-kind="([A-Za-z0-9_]+)"', attrs)
    if km:
        return km.group(1)
    if t.startswith('zan-'):
        return ''.join(p[:1].upper() + p[1:] for p in t[4:].split('-') if p)
    if t == 'input':
        tm = re.search(r'type="([A-Za-z0-9_]+)"', attrs)
        ty = tm.group(1).lower() if tm else ''
        if ty == 'checkbox':
            return 'Checkbox'
        if ty == 'radio':
            return 'Radio'
        if ty == 'range':
            return 'Slider'
        if ty == 'color':
            return 'ColorPicker'
        if ty == 'number':
            return 'InputNumber'
        if ty in ('date', 'month', 'week', 'time'):
            return 'DatePicker'
        if ty == 'file':
            return 'Upload'
        return 'Input'
    return {'label': 'Label', 'span': 'Label', 'button': 'Button',
            'textarea': 'TextArea', 'img': 'Image', 'hr': 'Divider',
            'progress': 'Progress', 'select': 'SelectBox',
            'table': 'DataGrid', 'ul': 'ListView', 'ol': 'ListView'}.get(t, 'Panel')


def named_fields(html):
    rows = []
    seen = set()
    for m in re.finditer(r'<([a-zA-Z-]+)\b([^>]*?)(/?)>', html):
        tag, attrs = m.group(1), m.group(2)
        if tag.lower() == 'body':
            continue
        idm = re.search(r'\bid="([A-Za-z0-9_]+)"', attrs)
        if not idm:
            continue
        fid = idm.group(1)
        if fid in seen:
            continue
        kind = tag_kind(tag, attrs)
        if not kind:
            continue
        seen.add(fid)
        om = re.search(r'data-of="([A-Za-z0-9_]+)"', attrs)
        rows.append((fid, kind, om.group(1) if om else ''))
    return rows


def code_behind(doc_dir, name):
    """找同名 code-behind，返回 (路径, 是否有 OnInit, using 行列表)。"""
    for p in glob.glob(os.path.join(SRC, '**', name + '.zan'), recursive=True):
        s = open(p, encoding='utf-8').read()
        has_init = re.search(r'\bvoid OnInit\s*\(\s*\)', s) is not None
        usings = re.findall(r'^using\s+[A-Za-z0-9_.]+;$', s, re.M)
        return p, has_init, usings
    return None, False, []


def emit(doc_path):
    name = os.path.basename(doc_path)[:-5]
    rel = os.path.relpath(doc_path, SRC).replace(os.sep, '/')
    html = open(doc_path, encoding='utf-8').read()
    fields = named_fields(html)
    cb, has_init, cb_usings = code_behind(os.path.dirname(doc_path), name)

    out = [HEADER.format(doc=name)]
    usings = ['using System;', 'using Gui;', 'using Gui.Widget;',
              'using Gui.Component;', 'using Gui.Component.DataTable;']
    for u in cb_usings:
        if u not in usings:
            usings.append(u)
    out.extend(usings)
    out.append('')
    out.append('partial class %s : DesignedControl {' % name)
    out.append('    %s() {' % name)
    out.append('        this.Design("%s", "ide/%s");' % (name, rel))
    if has_init:
        out.append('        this.OnInit();')
    out.append('    }')
    out.append('')
    for fid, kind, of in fields:
        typ = GENERIC.get(kind, kind).format(of=of or 'object')
        out.append('    public %s %s {' % (typ, fid))
        out.append('        get { return (%s)this.Ctl("%s"); }' % (typ, fid))
        out.append('    }')
    out.append('}')
    out.append('')

    dst = os.path.join(os.path.dirname(doc_path), name + '.design.zan')
    with open(dst, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out))
    return dst, len(fields), has_init


def main():
    docs = sorted(glob.glob(os.path.join(SRC, '**', '*.html'), recursive=True))
    written = 0
    designed = set()
    refs = set()
    for d in docs:
        html = open(d, encoding='utf-8').read()
        for m in re.finditer(r'<([a-zA-Z-]+)\b([^>]*?)(/?)>', html):
            k = tag_kind(m.group(1), m.group(2))
            if k:
                refs.add(k)
        if os.path.basename(d) == 'IdeForm.html':
            continue  # 窗口文档：入口手工投影，不生成半件
        dst, n, has_init = emit(d)
        designed.add(os.path.basename(d)[:-5])
        print('%s fields=%d onInit=%s' % (os.path.relpath(dst, ROOT), n, has_init))
        written += 1
    lines = registry_lines(refs, designed)
    reg = os.path.join(SRC, 'IdeDesignedRegistry.zan')
    with open(reg, 'w', encoding='utf-8', newline='\n') as f:
        f.write(REGISTRY.format(lines='\n'.join(lines)))
    print('wrote %d halves, registry kinds=%d' % (written, len(lines)))
    return 0


def registry_lines(refs, designed):
    """文档引用的 kind → 注册表行。设计类发工厂；声明了 Install 的
    组件类发调用；其余（内置/泛型容器/拼错名）不进表。"""
    lines = []
    for k in sorted(refs):
        qual = k
        if k not in designed:
            cf = class_file(k)
            if cf is None:
                continue
            text = open(cf, encoding='utf-8').read()
            if 'static void Install()' not in text:
                continue
            ns = re.search(r'^namespace\s+([A-Za-z0-9_.]+);', text, re.M)
            qual = ns.group(1) + '.' + k if ns else k
            lines.append('        %s.Install();' % qual)
            continue
        ns = ''
        cf = class_file(k)
        if cf is not None:
            ns = re.search(r'^namespace\s+([A-Za-z0-9_.]+);',
                           open(cf, encoding='utf-8').read(), re.M)
            ns = ns.group(1) if ns else ''
        full = ns + '.' + k if ns else k
        lines.append('        HeavyControls.Register("%s", (string k) => new %s());'
                     % (k, full))
    return lines


def class_file(name):
    for root in SCAN_ROOTS:
        for p in glob.glob(os.path.join(root, '**', name + '.zan'), recursive=True):
            if p.endswith('.design.zan'):
                continue
            return p
    return None


if __name__ == '__main__':
    sys.exit(main())
