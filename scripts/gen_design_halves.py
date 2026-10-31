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
    for d in docs:
        if os.path.basename(d) == 'IdeForm.html':
            continue  # 窗口文档：入口手工投影，不生成半件
        dst, n, has_init = emit(d)
        print('%s fields=%d onInit=%s' % (os.path.relpath(dst, ROOT), n, has_init))
        written += 1
    print('wrote %d halves' % written)
    return 0


if __name__ == '__main__':
    sys.exit(main())
