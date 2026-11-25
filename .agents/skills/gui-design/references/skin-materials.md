# 现代皮肤材质与工程不变量（gui-design 附册）

> 做高质感皮肤、调光影材质、查内置 16 套皮肤目录或排查 `skin.css` 级联异常时先查本册。

## 1. 内置 16 套皮肤目录（4×4）

写新皮肤前先查是否已有气质相近的内置皮肤可直接选用或作基线（唯一真源：`Skin.BuiltInNames()`）：

| 序号 | 名称 | 标签 | 明暗 | 材质基因 | 视觉定位与适用场景 |
| --- | --- | --- | --- | --- | --- |
| 0 | `porcelain` | 晴瓷 | 浅 | 静态不透明 + 双层轻影 | 默认商务清雅浅色，报表/工具/通用桌面 |
| 1 | `paper` | 素笺 | 浅 | 静态不透明 | 暖纸阅读与长文写作、知识库 |
| 2 | `sage` | 松烟 | 浅 | 静态不透明 | 低疲劳清绿自然办公、轻量编辑器 |
| 3 | `terracotta` | 暖陶 | 浅 | 静态不透明 | 暖红陶土编辑感、内容创作与品牌后台 |
| 4 | `midnight` | 墨蓝 | 暗 | 静态不透明 | 经典深蓝工程暗色，IDE/调试台 |
| 5 | `graphite` | 石墨 | 暗 | 静态不透明 | 中性无偏色石墨灰暗色，设计与色准敏感工具 |
| 6 | `forest` | 苔原 | 暗 | 静态不透明 | 深森护眼暗绿，长时间终端/监控面板 |
| 7 | `luxe` | 黑金 | 暗 | 静态不透明 + 双层影 + 金棱 | 暗金轻奢金融/展示/高端消费 |
| 8 | `cyber` | 霓虹 | 暗 | 静态不透明 + 双层影 + 霓虹辉光 | 深空青品红 HUD，极客监控/电竞工具 |
| 9 | `brutal` | 波普 | 浅 | `--brutal: 1` 硬边硬影 | 新粗野主义直角粗框高撞色，潮牌/活动页 |
| 10 | `frosted` | 琉璃 | 暗 | `--glass: 1; --gradient: 1` | 暗色极光磨砂玻璃，控制中心/启动器 |
| 11 | `sakura` | 绯樱 | 浅 | 静态不透明 | 柔和樱粉浅色，消费/社交/轻量应用 |
| 12 | `liquid` | 液态 | 浅 | `--glass: 1; --gradient: 1` | 亮色液态玻璃（日光流彩底 + 焦散折光棱） |
| 13 | `obsidian` | 极夜 | 暗 | 静态不透明 + 极细白光棱 | 精加工炭黑工程暗色（Linear/Vercel 质感） |
| 14 | `aurora` | 极光 | 暗 | `--gradient: 1; --glass: 0` | 暗色极光渐变底 + 丝绒不透明卡片（低开销） |
| 15 | `clay` | 柔陶 | 浅 | 静态不透明 + 双侧浮雕影 | 现代柔陶拟物（左上高光影 + 右下投影） |

## 2. 表面明度阶梯（Surface Luminance Ladder）

现代高质感界面靠**微弱而有序的明度阶梯**建立层级，而不是靠粗黑框线：

- **浅色系（禁止纯 `#ffffff` 铺满全局底）**：
  - `--bg-primary`（画布底）：微带环境色温的冷/暖灰（如 `#f3f5f9`、`#ece8e2`、`#f7f3e9`），让 `--bg-secondary`（卡片/面板，如 `#ffffff` 或 `#fbfcfd`）自然浮出；
  - `--bg-tertiary`（次级凹槽/输入框/禁用带）比 `--bg-primary` 再沉 1 档，形成“底板 → 卡片 → 浮层”三阶清晰分层。
- **暗色系（禁止纯 `#000000` 死黑）**：
  - 遵循 **Elevation = Brightness**：海拔越高的面越亮，每升一级明度递增 3%–5%：
    - `--bg-primary`（深底，如 `#0d0f13`）→ `--bg-secondary`（卡片，如 `#14171e`）→ `--bg-tertiary`（悬浮条/控件，如 `#1c2029`）→ `--bg-hover`（悬停，如 `#242936`）；
  - 暗色卡片边界优先用**极细半透明白光棱**（如 `border: 1 #22ffffff` 配 `-zan-sheen: 90 #ffffff`），取代生硬的不透明灰线，立即获得 Linear/macOS 级精加工质感。

## 3. OKLab 感知配色与自动令牌派生

sRGB 线性插值在蓝→紫、红→绿等跨色相过渡时会经过“灰泥中间色”。Zan.Gui 的颜色数学层（`ColorMath.LerpOklab` / `ColorMath.ShiftLightness`）已在 `Style.Defaults` 自动驱动以下派生令牌：

- `--primary-grad-end` / `--primary-hover-grad-end`：主按钮高光到微沉底部的感知均匀渐变；
- `--surface-grad-end` / `--titlebar-grad-end`：表面与标题栏微光泽梯度；
- `--btn-bg-end` / `--btn-hover-bg-end`：中性按钮微立体感；
- `--focus-ring` / `--primary-subtle`：由 `--primary` 自动派生半透明聚焦环与弱强调底。

**规则**：皮肤 `:root` 只需给准核心语义色，不必手搓一堆暗沉的中间过渡色；手写渐变停靠点时保持同色相或相邻色相小幅度明度偏移（`ΔL ≈ 0.03–0.08`），避免脏灰与塑料感。

## 4. 双层阴影（Dual-Layer `box-shadow`）配方

引擎原生支持逗号分隔的**两层阴影**（`box-shadow: layer1, layer2;`，先画第 2 层后画第 1 层，与浏览器叠放顺序一致；超过 2 层会被忽略）：

1. **Contact + Ambient 物理抬升影（现代浅/暗卡片与浮层标配）**：
   - 第 1 层（Contact 接触影）：小位移、小模糊、稍深透明度，锚定边缘不发虚；
   - 第 2 层（Ambient 环境影）：大位移、大模糊、低透明度，营造悬浮空间感。
   - 浅色卡片参考：`box-shadow: 0 1 2 #14122038, 0 6 18 #18122038;`
   - 浅色浮层/弹窗参考：`box-shadow: 0 2 5 #1a122038, 0 14 34 #26122038;`
   - 暗色卡片参考：`box-shadow: 0 1 2 #58000000, 0 8 22 #44000000;`
2. **柔陶 / 新拟物双侧浮雕影（Claymorphism / Neumorphism）**：
   - 第 1 层（左上高光影）：负偏移 + 半透明暖白（`-3 -3 9 #dcffffff`）；
   - 第 2 层（右下投影）：正偏移 + 柔和冷灰影（`4 5 14 #2620283a`）；
   - 必须配合同色系微亮顶边高光（`-zan-sheen`）与低对比描边，输入框可切凹槽底色区分可编辑区。
3. **主按钮与聚焦环辉光（Colored Glow & Focus Ring）**：
   - 主按钮悬停微抬：`box-shadow: 0 2 6 #38<primary-rgb>, 0 6 16 #26<primary-rgb>;`
   - 输入框聚焦环：`border-color: var(--border-focus); box-shadow: 0 0 8 #50<focus-rgb>;`
4. **约束**：`--brutal: 1` 模式会自动清零第 2 层阴影以保持硬朗单层偏移；静态玻璃缓存（`StaticGlassEligible`）在启用双层阴影时会回退常规绘制路径以保证完整阴影不被裁切。

## 5. 液态玻璃与磨砂玻璃光学配方（Liquid Glass & Frosted）

开启 `--glass: 1` 与 `--gradient: 1` 后，框架会按背景明暗（`theme.IsLightBg()`）自动切换两套静态缓存壁纸（零每帧额外 CPU 开销）：
- **暗色玻璃（`frosted`）**：深海极光多色光斑底板；
- **亮色液态玻璃（`liquid`）**：日光流彩折射光斑底板（冰蓝/天青/薄紫/微粉柔和漫射）。

核心光学参数与调校边界：

- `--glass-blur`：推荐 `24–36`（过低杂乱，过高糊成单色失掉折射感）；
- `--glass-tint`（千分比 `0–1000`）：
  - 亮色液态玻璃推荐 `620–740`（既透出流彩，又保证深色文字 ≥ 7:1 对比度）；
  - 暗色磨砂玻璃推荐 `340–460`（透出极光同时稳住白字可读性）；
- `--glass-chrome-tint`：标题栏/导航栏等壳层比卡片更实一档（通常 `+120..+200`）；
- **`-zan-sheen: <strength> [<color>]` 光学折光棱**：
  - `strength` 范围 `0–400`；绘制顶部柔和反光带 + 1px 内环高光 + 1px 顶沿镜面棱；
  - 当 **`strength >= 220` 且高度 `>= 18`** 时，引擎额外激活**底部光学焦散反射棱（Bottom Caustic Bounce Rim）**，形成液态玻璃特有的“上下双沿通透折光”；
  - 常规不透明皮肤建议 `60–160`（微弱机加工棱线），液态/琉璃玻璃建议 `240–320`。

## 6. 对比度与交互状态底线（可读性不可妥协）

再精致的材质也不能牺牲信息可读性，所有皮肤交付前必须满足：

- **正文对比度**：`--text-primary` 相对 `--bg-primary` 与 `--bg-secondary` 不低于 WCAG AA **4.5:1**（目标 ≥ 7:1）；
- **次级与禁用**：`--text-secondary` 不低于 **3.5:1**；`--text-disabled` 明显弱化但不与底色融为一体；
- **主按钮反色文字**：`--text-inverse` 必须按 `--primary` 真实亮度选深/浅——亮青（`#4de4ff`）、亮金（`#d4af37`）、浅蓝（`#7cc4ff`）等高亮度主色配深字（如 `#0a1420`），中深主色配纯白（`#ffffff`）；
- **状态五件套齐全**：按钮、输入控件、标签页、列表项的 **常态 / 悬停(`:hover`) / 按下(`:active`) / 聚焦(`:focus`) / 禁用(`:disabled`)** 全部清晰可辨，尤其输入框与可聚焦控件必须有明确聚焦边框或聚焦环。

## 7. `skin.css` 两阶段级联四大工程红线（真实踩坑汇总）

皮肤的 `skin.css` 在 `base.css` 之后作为第二阶段合并，选择器写宽了会静默破坏内置组件契约：

1. **卡片规则必须排除抽屉表面（`.drawer-surface`）**：
   - 正确写法：`card, panel.card:not(.drawer-surface) { ... }`
   - 理由：`Drawer` 内部面板带 `.card` 类，且需要保持实底不透明以承载抽屉内容；写成裸 `panel.card` 覆盖半透明底或背景渐变会把抽屉打穿并击穿像素回归测试。
2. **主按钮辉光/光泽规则必须排除非实心变体**：
   - 正确写法：`button.primary:not(.ghost):not(.secondary):not(.text):not(.outline):not(.dashed):not(.tertiary):not(.quaternary)`
   - 理由：`.secondary.primary`、`.ghost.primary` 等变体同挂 `.primary` 类，直接写 `button.primary { box-shadow: ...; -zan-sheen: ...; }` 会让透明/幽灵按钮错长实心光晕。
3. **覆写输入框背景时必须同步守住禁用态**：
   - 凡写了 `input, textarea, select { background: ...; }`，下方必须紧跟 `input:disabled, textarea:disabled, select:disabled { background: var(--bg-disabled); }`；
   - 理由：`skin.css` 在 `base.css` 之后匹配，裸覆写会盖掉 `base.css` 的禁用底色，让禁用输入框看起来像可编辑。
4. **禁止在皮肤里裸写 `label { color: ...; }`**：
   - 理由：`base.css` 刻意用 `label:not(.button-caption):not(.tab-item-title)` 让按钮标题与标签页标题继承父控件前景色；皮肤里若裸写 `label { color: var(--text-primary); }`，会导致深色主按钮或选中标签上的文字锁死成正文色而完全看不清。
5. **CSS 语法卫生**：
   - 8 位十六进制是 `#AARRGGBB`（alpha 在前），切勿按 Web 习惯写成 `#RRGGBBAA`；
   - 注释正文内禁止出现 `*/` 字符序列（注释卫生门禁会拦截）。

## 8. 新增皮肤与视觉验收闭环

- **应用私有皮肤**：放在应用目录 `skins/<name>/skin.css`（声明 `--name`、`--order` 及 `:root` 令牌即可，应用侧优先于内置皮肤）。
- **仓库内置皮肤（四处同步，缺一不可）**：
  1. 新建 `packages/Zan.Gui/skins/<name>/skin.css`（写清 `--name`、`--order`）；
  2. 注册到 `packages/Zan.Gui/src/Gui/Styling/Skin.zan` 的 `BuiltInNames()`；
  3. 同步到 `packages/Zan.Gui/zan.pkg` 的 `baseline=` 编译期内嵌皮肤列表；
  4. 更新契约与像素测试（`tests/conformance/gui_skin_theme_vars.zan`、`tests/gui/theme_drawer_retained_input_test.zan`）。
- **交付验证三步走**：
  1. 跑 `composed.Lint()` 确保 `SKIN_LINT_ISSUES == 0`（无无效属性、无被丢弃选择器）；
  2. 离屏或实机渲染全控件样板（标签页、卡片、主/次/幽灵/状态/禁用按钮、徽标、进度条、滑块、复选/开关、常态/聚焦/禁用输入框、浮层卡片）并**逐张肉眼验收**；
  3. 跑齐皮肤变量、抽屉像素、配色/字号预算与注释卫生回归测试。
