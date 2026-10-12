# SkinBuilder — Zan Gui Chart 配色配置工具

所见即所得的 Chart 组件配色编辑器：左边调色，右边是**真实 Chart
组件**的实时预览，每一帧把当前配置写成 draft 皮肤并走 `App` 的皮肤
热重载通道，预览零延迟跟手。全部使用标准库组件
（ColorPicker / Input / RadioGroup / Button / TextArea / Label / Panel +
Chart），无任何自绘。

**图表皮肤不是整套 UI 皮肤。** 工具的产物只是一段可复制的 CSS 文本
（`--chart-1..8` + `chart::*` 规则），与标题栏的皮肤选择无关——draft
带 `--kind: "chart"` 标记，`Skin.Names` 不会把组件皮肤列进 UI 皮肤
选择器；粘进应用 skin.css 的图表配色也不会让该皮肤从选择器里消失
（只要皮肤本身没有 `--kind: chart` 标记）。

## 构建与运行

在 ZanIDE 里打开 `SkinBuilder.zan` 一键运行即可；命令行等价形式：

```bash
build/zanc tools/skinbuilder/SkinBuilder.zan --auto-stdlib -o build/skinbuilder.exe
```

`--auto-stdlib` 自动拉齐 Gui/Widget/Chart 组件，`[DllImport("zan_gui")]`
的原生驱动由 zanc 从 `packages/Zan.Gui/src/Gui/drivers/<平台>/` 自动发现并捆绑到
输出旁，无需任何手工链接步骤。

## 界面

| 区域 | 内容 |
|------|------|
| 左列 A | 皮肤名称（进产物注释）、系列调色板 `--chart-1..8`（初值 = ECharts 2.x 默认定性色）、预览基底（亮/暗，只影响预览）、重置/复制按钮 |
| 左列 B | 图表部件颜色：grid / axis / 刻度文字 / 轴标题 / 数据标签 label / legend / mark / accent / tooltip 底·字·边 |
| 右侧 | 两块真实 Chart 预览：折柱混搭（覆盖调色板 1..3、轴、网格、数据标签、图例、平均线标记）+ 环形占比图（调色板 1..5、扇区标签、中心合计） |
| 底部横条 | **全部产物**：随配置实时刷新的 CSS 文本，可全选复制 |

## 产出：一段可复制的 CSS

「复制 CSS」把底部文本框内容一键送进系统剪贴板；文本本身也随配置
实时刷新。生成的 css 形如：

```css
/* my-chart-skin — Zan Gui Chart 组件皮肤（SkinBuilder 生成，整段粘进应用 skin.css） */
:root {
  --kind: "chart";        /* 组件皮肤标记：单独成包时不进 UI 皮肤选择器 */
  --chart-1: #C1232B;     /* Chart.Palette 的第 1 槽，以此类推到 --chart-8 */
  ...
}
chart::grid    { background: #CCCCCC; }
chart::axis    { background: #4488BB; color: #333333; }
chart::axis-title { color: #6E7079; }
chart::legend  { color: #6E7079; }
chart::label   { color: #333333; }
chart::mark    { background: #E43C4E; color: #E43C4E; }
chart::accent  { background: #4488BB; }
chart::tooltip { background: #FFFFFF; color: #333333; border: 1 #DDDDDD; }
```

## 用法：粘进应用的 skin.css

把整段 CSS 追加到应用自己的皮肤 css（或任意会被加载的 skin.css）
即可——`:root` 令牌逐层叠加，`--chart-*` 只接管 Chart 组件的调色
回退，`chart::*` 规则只接管图表部件颜色，其余外观完全不动。

draft（`<exe目录>/skins/skinbuilder-draft/skin.css`）只是本工具预览
热重载的内部机制：带 `--kind: "chart"`，任何应用的 UI 皮肤选择器都
不会列出它，工具退出时也会自动删除。想让配色脱离工具常驻，就用
「复制 CSS」粘进你自己的 skin.css。
