# Zan Visual Identity · Zan 视觉识别系统

> 闪电是 Zan 的心跳：AOT 编译像闪电一样快，代码像 Zanzi 一样软萌可爱。
> 标语：**AOT. ARC. Adorable.**

本目录是 Zan 语言的完整 VI 资产。矢量源（SVG）与全部重建脚本随仓库维护，
位图均可由源文件或 prompt（`tools/prompts.md`）重新生成（见 §6 重建）。

```
assets/brand/
├── VI.md                  本规范
├── logo/                  矢量源（SVG 为唯一权威源）
│   ├── zan-mark.svg           主图标：电光蓝渐变圆角方 + 白兔头 + 金色火花
│   ├── zan-mark-mono.svg      单色墨版（浅底/单色印刷）
│   ├── zan-mark-white.svg     全白剪影版（深底）
│   ├── zan-wordmark.svg       "zan" 圆角几何字标（墨）
│   ├── zan-wordmark-white.svg 字标（白）
│   ├── zan-logo-horizontal.svg(-dark)  横排组合
│   ├── zan-logo-vertical.svg           竖排组合
│   └── png/                 各尺寸 PNG（512…16）
├── mascot/                吉祥物 Zanzi（透明底 PNG）
│   ├── zanzi-glyph.svg       头标 glyph（符号阶梯最低档、矢量锚点）
│   ├── png/                  头标各尺寸 PNG（256…32）
│   ├── zanzi-hero.png        立绘·挥手（主视觉）
│   ├── zanzi-avatar(-round).png  头像（方/圆）
│   ├── zanzi-run.png         奔跑（构建中/极速）
│   ├── zanzi-sleep.png       睡觉（空闲/挂机）
│   ├── zanzi-cheer.png       欢呼·举手（发布/成功）
│   └── zanzi-code.png        敲码（开发中）
├── banner/
│   ├── zan-banner.png        GitHub README 横幅 1500×500
│   └── zan-palette.png       色板总览卡
└── tools/                 重建脚本（无外部依赖，Python + PIL + Edge）
    ├── render_pngs.py        SVG → 多尺寸 PNG
    ├── compose.py            合成横幅与色板卡
    ├── postprocess.py        image2 原图 → 抠底/切尺寸/zan.ico/iOS 图标
    └── prompts.md            image2 生成 prompt 权威存档
```

## 1. 品牌理念

| 气质 | 视觉转译 |
|------|----------|
| 高性能 | 兔子（弹射起步的疾速）、闪电符号、电光蓝渐变 |
| 干练 | 几何圆角笔画、克制的单点缀、大留白 |
| 萌 | 吉祥物 Zanzi：圆胖肉感、大眼、粉耳、绒球尾 |

## 2. 色板

| 名称 | HEX | 用途 |
|------|-----|------|
| Zan Volt | `#2B5CFF` | 主色：图标底、链接、强调按钮 |
| Volt Deep | `#2141E6` | 渐变深端 / hover |
| Zan Sky | `#7DA2FF` | 浅色 tint：选中态底、暗色环境中的次级文字 |
| Zan Spark | `#FFC838` | 唯一金色点缀：火花、星光；**不可大面积铺底** |
| Zan Blush | `#FFB3C7` | 兔耳内耳/鼻头/腮红，柔和点缀；**只用于小面积** |
| Zan Cream | `#FFF6E8` | 暖色表面（吉祥物毛色同源） |
| Zan Ink | `#0F1428` | 正文/深色背景 |

语义色：OK `#2FBF71` · Warning `#FF8A3D` · Error `#FF5C5C` · Info `#2B5CFF`（同 Volt）。

中性灰阶（由 Ink 向 Paper 过渡）：`#F4F6FB / #D9DEEC / #9AA5C0 / #5A6480 / #2A3150`。

## 3. Logo

**兔头砖图标** = 圆角方砖（Volt 渐变）+ 白兔头（长耳、Blush 粉内耳）+ 右上金色火花。
火花是唯一金色记忆点；粉色只出现在耳内与鼻头，其他一切保持安静——这是"干练"的体现。

- **安全区**：图标四周留出砖面宽度 1/8 的空白。
- **最小尺寸**：带砖图标 16px（耳与火花仍可辨；<32px 时视觉上退化为"蓝砖白点"亦可接受）。
- **变体规则**：浅底用 `zan-mark` / `-mono`；深底（Ink/深蓝）用 `zan-mark` 或 `zan-mark-white`（全白剪影）；单色印刷/传真场景用 `-mono`（粉耳转墨版挖空）。
- **组合**：优先横排 `zan-logo-horizontal`；方形容器（头像、App 图标）只用 mark 不带字标。

**禁用**：拉伸变形 / 更换色相 / 给兔头描彩边或投影 / 火花换成其他符号 /
字标单独描色 / 把 mark 放在低于 4.5:1 对比度的背景上。

## 4. 字体

| 场景 | 首选 | 回退 |
|------|------|------|
| 界面/文档标题正文 | Inter | Segoe UI、苹方、微软雅黑、思源黑体 |
| 代码/终端 | JetBrains Mono | Cascadia Code、Consolas |
| 中文 | 思源黑体 / 微软雅黑 | — |

"zan" 字标是**手绘矢量路径**（圆头几何笔画，笔画宽 38 / x 高 150，见 SVG），
不依赖任何字体文件，任何环境渲染一致。正文排版不要模仿字标风格。

## 5. 吉祥物 Zanzi（赞崽）

**人设**：一只圆滚滚的小兔子。像 AOT 编译一样弹射起步，像 ARC 一样省心，
像值语义一样不给你添乱。性格：精力充沛、干劲十足、编译通过就举爪欢呼，
偶尔就地睡着。

**三个锚定特征**（任何二创必须保留，缺一不可）：

1. 长耳 + **Blush 粉内耳**
2. 脖子上**钴蓝围巾**（Zan Volt 同族色，颈间侧结、结在角色右侧）
3. **电光蓝绒球尾**（Volt 色相的小圆绒球，闪电的柔软化身）

**图案规则**（可复制性是这条设计的核心约束，二创请遵守）：

- 身体**圆胖有肉感**：梨形身、圆肚、胖腮、粗短四肢——不许画瘦长体形
- 毛色**奶油白单色**（#FFF6E8），无斑点无杂纹；粉色只出现在内耳/鼻头/腮红
- 上色是**柔和体积感**（微渐变 + 高光），不许退回纯平涂
- 除围巾/绒球尾/粉色三处外**不得引入新颜色**

标准姿态五张（见 `mascot/`）：hero 挥手 / run 奔跑 / sleep 睡觉 / cheer 举爪 / code 敲码；
头像（方/圆）与头标 glyph 从 hero 派生。

**符号阶梯**（从大到小逐级抽象，每级都可独立使用）：

全身立绘 → 头像 → **头标 glyph**（zanzi-glyph.svg，favicon/群头像档）→
长耳剪影 / 电光蓝绒球（可单独作装饰符号，与 logo 火花呼应）。

**用法**：透明底 PNG 可直接叠在 Volt、Ink、Cream 底色上。
建议按语境配姿态：构建中→run，发布成功→cheer，空闲→sleep，教程→code。

**禁用**：拉伸比例 / 更换色相 / 画瘦 / 去掉三个锚定特征 / 给身体加斑点 /
加上文字气泡或水印 / 用于与 Zan 无关的产品。

## 6. 重建

```bash
# SVG → PNG（需要 Edge，见 tools/render_pngs.py 顶部常量）
python assets/brand/tools/render_pngs.py
# 吉祥物/ico/iOS 图标：把 image2 原图（绿幕底 1024²）喂给 postprocess
python assets/brand/tools/postprocess.py <原图目录>   # prompt 见 tools/prompts.md
# 横幅与色板卡（需要 PIL）
python assets/brand/tools/compose.py
```

`assets/zan.ico`（编译进 zanc 的默认 exe 图标）与 `assets/zan_ios_icon.png`
由 `postprocess.py` 从 image2 图标原图产出。吉祥物原图为 AI 生成
（gpt-image-2），prompt 权威存档在 `tools/prompts.md`；成品即权威资产。

## 7. 资源清单与来源

下面这份清单按**仓库里实际存在的文件**列：像素尺寸读自 PNG 头（IHDR）与
SVG `viewBox`，不是估算；"引入提交"是 `git log --diff-filter=A` 查到的首个
入库提交。

### 7.1 矢量源（手绘几何路径 · 唯一权威）

| 文件 | 画布 | 用途 | 引入提交 |
|------|------|------|----------|
| `logo/zan-mark.svg` | 512×512 | 主图标：Volt 渐变砖 + 白兔 + 金色火花 | `3f90649c` |
| `logo/zan-mark-mono.svg` | 512×512 | 单色墨版（浅底/单色印刷） | `3f90649c` |
| `logo/zan-mark-white.svg` | 512×512 | 全白剪影（深底） | `3f90649c` |
| `logo/zan-wordmark.svg` | 640×320 | "zan" 圆角几何字标（墨） | `3f90649c` |
| `logo/zan-wordmark-white.svg` | 640×320 | 字标（白） | `3f90649c` |
| `logo/zan-logo-horizontal.svg` | 985×512 | 横排组合 | `3f90649c` |
| `logo/zan-logo-horizontal-dark.svg` | 985×512 | 横排组合（深底） | `3f90649c` |
| `logo/zan-logo-vertical.svg` | 640×796 | 竖排组合 | `3f90649c` |
| `mascot/zanzi-glyph.svg` | 256×256 | 头标 glyph（符号阶梯最低档） | `8a25e2aa` |

### 7.2 位图（PNG · 三类来源）

**① 由矢量渲染**（`tools/render_pngs.py`：Edge 无头渲染 → 抠洋红底 →
PIL 降采样，透明底）：`logo/png/` 下 `zan-mark-16/32/48/64/128/256/512`、
`zan-mark-mono-256/512`、`zan-mark-white-512`、`zan-wordmark-640`、
`zan-wordmark-white-640`、`zan-logo-horizontal-985`、
`zan-logo-horizontal-dark-985`、`zan-logo-vertical-640`，以及
`mascot/png/zanzi-glyph-32/64/128/256`。这批**可由 SVG 完全再生**。

**② 由 AI 生成原图加工**（`tools/postprocess.py`：绿幕抠底 + 切尺寸）：
`mascot/` 下 `zanzi-hero` 672×1024、`zanzi-run` 903×949、
`zanzi-sleep` 976×877、`zanzi-cheer` 736×1024、`zanzi-code` 801×1008、
`zanzi-avatar` 512×512、`zanzi-avatar-round` 512×512。原型为 gpt-image-2
的 1024×1024 绿幕原图，prompt 权威存档在 `tools/prompts.md`；脚本幂等。

**③ 脚本合成**（`tools/compose.py`，需 PIL）：`banner/zan-banner.png`
1500×500（GitHub 横幅）、`banner/zan-palette.png` 1600×1180（色板总览卡）。

### 7.3 品牌派生物（不在本目录，但同源）

| 文件 | 大小 | 来源 | 引入提交 |
|------|------|------|----------|
| `assets/zan.ico` | 105,903 B | `postprocess.py` 从图标原图产出 | `da453a10`（2026-07-06） |
| `assets/zan_ios_icon.png` | 60,423 B | 同上，iOS 图标 | `0169e32d`（2026-10-16） |
| `assets/zan.rc` | 18 B | Windows 资源脚本（`1 ICON "zan.ico"`） | `da453a10`（2026-07-06） |

### 7.4 仓库内消费点

| 资源 | 谁在用 |
|------|--------|
| `assets/zan.ico` | `CMakeLists.txt:62` 的 `ZAN_DEFAULT_ICON` → 编译进 zanc，所有 zanc 产出的 exe 默认图标；`scripts/build_ide.ps1:192`、`build_gallery.ps1:41`、`build_gallery_single.ps1:73`、`build_charts.ps1:49` 的 `--icon`；`src/ide_zan/src/services/ZanIDE.Scaffold.zan:496` 给新项目脚手架复制；`scripts/stage_dev_toolchain.ps1:192` 随 dev toolchain 分发；`scripts/publish_ide.ps1:176` 进发布包 |
| `assets/zan_ios_icon.png` | `CMakeLists.txt:63` 的 `ZAN_DEFAULT_IOS_ICON` |
| `assets/zan.rc` | Windows 资源编译 |
| 本目录下的 logo/吉祥物/横幅位图 | **仓库内暂无程序消费点**（对外物料）；`zan-site/` 用的是站点自带的 `zan-site/assets/mark.svg`，没有引用本包 |

### 7.5 来源可追溯性

- 品牌体系整体：`3f90649c`（2026-09-23 "Zan VI 体系上线"）；头标与符号
  阶梯：`8a25e2aa`（同日"吉祥物重塑……矢量母版+符号阶梯"）。
- `tools/prompts.md`、`tools/postprocess.py` 随 `7c71ecec`（2026-10-20）
  纳入版本控制——脚本与 prompt 存档都在仓内，重建链不依赖外部凭据。
- 尺寸的约束力分两类：①③（SVG 渲染 / 脚本合成）由脚本固定；②（AI 原图
  加工）取决于原图与裁切，换 prompt 重新生成后像素可能变化——下游按
  **文件名与用途**引用，别按具体像素写死布局。

