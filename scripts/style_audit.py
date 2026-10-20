#!/usr/bin/env python3
"""样式审计：产品壳的 CSS 里不允许再出现裸像素的间距/尺寸与裸时长。

背景：这套界面曾经有**五套尺子**同时在跑——Space/Radius/WidgetSize 三份
"规范"常量（几乎没人用）、Theme 的默认度量（真正生效的那一套）、
base.css 里手写的裸像素、ide.css 里手写的裸像素、以及 14 套皮肤包各自的
裸像素。相邻表面的内边距因此对不齐（同一个"卡片内衬"可能是 8 也可能是
18），这是"看着不精致"最主要的来源。现在统一到一处：

  * 规范：packages/Zan.Gui/src/Gui/Styling/{Space,Radius,WidgetSize,Motion}.zan
  * 主题度量：Theme 默认值取自上面四份（padding 4/8/12/16、gap 4/8/12、
    radius 4/8/12、height 24/28/32/40、icon 16/20/24、动效 100/200/300）
  * CSS 令牌：Style.RootFromTheme 导出 --padding-*/--gap-*/--radius-*/
    --height-*/--icon-size-*/--font-size-*（主题度量，可被皮肤覆写）、
    --space-xs..--space-xl（逻辑像素阶梯，供原子类用）与
    --anim-fast/--anim-medium/--anim-slow（动效三档）

本脚本是这条约束的守门人，检查三件事：

  1) **间距类属性**（padding*/margin*/gap*/row-gap/column-gap/border-radius）
     只能写令牌，或写阶梯上的整数（0/1/2/4/8/12/16/24/32/48）。出现别的
     裸像素即失败——这是"全局统一"能守住的关键。
  2) **字号**（font-size）只能写 --font-size-* 令牌。
  3) **动效时长**（transition / animation-duration）只能写 --anim-* 令牌：
     响应反馈的速度只有 fast/medium/slow 三档，各皮肤各写各的
     `180ms` / `220ms` 就是"手感不一致"的来源。

尺寸类属性（width/height/min-*/max-*）只**报告不失败**：它们多是控件
几何（指示器 12px、滑块点 10px、图片 300×220），与间距阶梯不是一回事，
硬套阶梯反而会破坏形状。

氛围/装饰性循环（`animation: glow 2600ms` 这类背景呼吸、光晕、轮播
间隔）不在这三档里：它们的节奏由内容决定，挤进同一把尺子反而丢掉了
"这个皮肤就是慢吞吞"这层性格。因此只管 transition / animation-duration。

用法：
    python scripts/style_audit.py            # 审计受管文件
    python scripts/style_audit.py --all      # 连皮肤包一起审计（默认只报不算失败）
    python scripts/style_audit.py --json out.json
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 受管文件：产品壳的样式表。皮肤包（packages/Zan.Gui.Themes/skins/*）是
# 各皮肤自己的观感，允许更有个性，按需用 --all 一并报出。
MANAGED = [
    'packages/Zan.Gui/src/Gui/skins/base.css',
    'src/ide_zan/assets/ide.css',
]
SKINS = ['packages/Zan.Gui.Themes/skins/*/skin.css']

# 间距阶梯：Space 的 8pt 网格（0/2/4/8/12/16/24/32/48）。
LADDER = {0, 1, 2, 4, 8, 12, 16, 24, 32, 48}

# 失败项：间距与圆角、字号。
SPACING_PROPS = (
    r'padding(?:-(?:top|right|bottom|left))?'
    r'|margin(?:-(?:top|right|bottom|left))?'
    r'|(?:row|column)-gap|gap|border-radius'
)
STRICT_PROPS = SPACING_PROPS + r'|font-size'

# 只报告不失败：控件几何。
REPORT_PROPS = r'width|height|min-width|min-height|max-width|max-height'

# 动效时长：响应反馈只有三档，只能引用 --anim-* 令牌。
MOTION_PROPS = r'transition|animation-duration'
VAR_REF = re.compile(r'var\(--[a-z0-9-]+(?:,[^)]*)?\)')

DECL = re.compile(r'(?P<prop>[a-z-]+)\s*:\s*(?P<value>[^;{}]+)')


def audit_motion(lineno, prop, value):
    """动效时长只能用 --anim-* 令牌；返回一条错误或 None。"""
    refs = VAR_REF.findall(value)
    for ref in refs:
        name = ref[4:].split(',')[0].strip()
        if not name.startswith('--anim-'):
            return (lineno, prop, value,
                    '动效时长只认 --anim-* 令牌（这里是 %s）' % name)
    rest = VAR_REF.sub('', value)
    if re.search(r'\d', rest):
        return (lineno, prop, value,
                '动效时长必须用 var(--anim-fast/medium/slow)')
    return None


def audit_file(path, strict_props=STRICT_PROPS, report_props=REPORT_PROPS):
    """返回 (errors, warnings)；errors 里的每项是 (行号, 属性, 值, 原因)。"""
    errors = []
    warnings = []
    with open(path, 'r', encoding='utf-8') as fh:
        for lineno, line in enumerate(fh, 1):
            if line.lstrip().startswith('/*') or line.lstrip().startswith('*'):
                continue
            for m in DECL.finditer(line):
                prop = m.group('prop').lower()
                value = m.group('value').strip()
                if re.fullmatch(r'(?:%s)' % MOTION_PROPS, prop):
                    bad = audit_motion(lineno, prop, value)
                    if bad:
                        errors.append(bad)
                    continue
                if not re.fullmatch(r'(?:%s)' % strict_props, prop):
                    if re.fullmatch(r'(?:%s)' % report_props, prop):
                        warnings.append((lineno, prop, value))
                    continue
                # 允许：令牌、百分比、auto、关键字、多值里的 auto/百分比。
                if 'var(--' in value:
                    continue
                if not re.search(r'\d', value):
                    continue
                nums = [int(n) for n in re.findall(r'(?<![\w.-])(\d+)(?:\.\d+)?', value)]
                if not nums:
                    continue
                # px 单位的值按逻辑像素看待（引擎按显示缩放），阶梯判定不变。
                bad = [n for n in nums if n not in LADDER]
                if bad:
                    errors.append((lineno, prop, value,
                                   '不在间距阶梯 %s 上' % sorted(LADDER)))
    return errors, warnings


def collect(paths, strict):
    all_errors = []
    all_warnings = []
    for path in paths:
        if not os.path.exists(path):
            continue
        errors, warnings = audit_file(
            path,
            strict_props=STRICT_PROPS if strict else SPACING_PROPS,
            report_props=REPORT_PROPS if strict else '')
        rel = os.path.relpath(path, ROOT).replace('\\', '/')
        for lineno, prop, value, why in errors:
            all_errors.append('%s:%d  %s: %s  (%s)' % (rel, lineno, prop, value, why))
        for lineno, prop, value in warnings:
            all_warnings.append('%s:%d  %s: %s' % (rel, lineno, prop, value))
    return all_errors, all_warnings


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--all', action='store_true',
                    help='连皮肤包一起审计（只报间距类问题，不因它们失败）')
    ap.add_argument('--verbose', action='store_true',
                    help='把控件几何（width/height 等）的提示也逐条列出')
    ap.add_argument('--json', help='把结果写到这个文件')
    args = ap.parse_args()

    managed = [os.path.join(ROOT, p) for p in MANAGED]
    errors, warnings = collect(managed, strict=True)

    skin_errors = []
    if args.all:
        skins = []
        for pattern in SKINS:
            skins.extend(glob.glob(os.path.join(ROOT, pattern)))
        skin_errors, _ = collect(sorted(skins), strict=False)

    for line in errors:
        print('STYLE    ' + line)
    for line in skin_errors:
        print('SKIN     ' + line)
    if args.verbose:
        for line in warnings:
            print('GEOMETRY ' + line)

    print('---')
    print('受管文件 %d，越界 %d，皮肤包越界 %d，几何提示 %d'
          % (len(managed), len(errors), len(skin_errors), len(warnings)))

    if args.json:
        with open(args.json, 'w', encoding='utf-8') as fh:
            json.dump({'errors': errors, 'skin': skin_errors,
                       'geometry': warnings}, fh, ensure_ascii=False, indent=2)

    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
