# Zan Chart 引擎 · 代码级对照账本（2026-09-11）

> 口径：**按 ECharts 6.1 源函数/源文件记账**，不按 demo 记账、不按截图记账。
> 权威源 `_scratch/echarts-master/src/`（v6.1.0，596 个 `.ts`）。
> 目标 `stdlib/Gui/Component/Chart/*.zan`（35 文件 / 31,759 行）。
>
> 本文是 `docs/CHART_COMPATIBILITY_5.md`「迁移账本」的**代码侧补充**：
> 那份账本按官方示例记进度，本文按引擎代码记缺口。两者冲突时以本文为准，
> 因为"示例能不能画"是结果，"引擎有没有这段逻辑"才是原因。
>
> 复现：`python scripts/chart_gap_audit.py`（见文末）。

---

## 一、结论先说

"图表有大量错误"这一判断成立，且可以量化：

**这台引擎的绝大多数代码不是照 ECharts 源写的。**

| 度量 | 数值 |
|------|------|
| ECharts 6.1 源文件总数 | 596 |
| 被引擎代码引用过的源文件数 | **14** |
| 引用过源的引擎文件数 | **6 / 35** |
| 官方 option 用到的配置键 | 470 |
| 引擎**从不读取**的配置键 | **278（59%）** |

关键在于**引用哪些**：14 个被引用的源里，**没有一个**是决定图表正确性的
核心布局/刻度/系列/视图文件——`barGrid.ts`、`BarView.ts`、`LineSeries.ts`、
`PieView.ts`、`Scale.ts`、`OrdinalScale.ts`、`axisNiceTicks.ts`、`symbol.ts`、
`SymbolDraw.ts`、`LegendView.ts`、`TooltipView.ts`、`ChartView.ts`、
`axisBand.ts`、`scaleRawExtentInfo.ts`、`dataZoom/*`、`visualMap/*` 全部未引用。
被引用的只有 `poly.ts`（平滑贝塞尔）、`number.ts`、`helper.ts`（刻度取整）、
`tokens.ts`（色板）、`Interval.ts:183`、`Calendar*`、`LineView.ts`（局部）。

**这解释了"有些完全就是错的"**：`ChartType` 枚举的注释自己写着
「ECharts 2.2.7 原生 series.type」（`ChartModel.zan:8`），`ChartViewBar.zan`
的柱宽公式带 7 处「ECharts2」标注。引擎的骨架是 2.2.7 时代的手写实现，
v6 特性是逐 demo 打补丁加上去的。配置一样但**语义层是 2.2.7**，
画出来自然错。

---

## 二、已核实的「错误」（实现了，但与源语义不符）

每条都给出**双侧行号**，可直接复核。

### W1 · 柱宽/柱间距：手写常数 vs 源求解 `barGrid.ts:206-349`

- Zan `ChartViewBar.zan:351-354`：
  `groupW = step*7/10`、`gap = Scale(2)`、`barW = (groupW - gap*(n-1))/n`。
  7/10 与 2px 都是**硬编码常数**。
- ECharts `layout/barGrid.ts:255-263,270`：
  `barCategoryGap` 缺省 = `max(35 - stackIdList.length*4, 15)%`（按堆叠组数变化）；
  `barGap` 缺省 `'10%'`，且 `parsePercent(barGapOption, 1)` —— 是**柱宽的**比例，
  不是像素；`autoWidth = (remainedWidth - barCategoryGapNum) /
  (autoWidthCount + (autoWidthCount-1)*barGapPercent)`。
- 引擎源码中 `barCategoryGap` 出现 **0 次**。
- 影响：**全部 40 个 bar demo**（含极坐标柱），且类目带宽是所有类目轴图的地基。

### W2 · 折线默认符号：漏了 v6 的两个提前返回

- Zan `ChartViewLine.zan:1114-1124`：auto（`showSymbol=-1`）时**无条件**把符号
  收窄到"有可见标签的主刻度"，只画刻度位置的符号。
- ECharts `chart/line/LineView.ts:371-410` `getIsIgnoreFunc()`：
  ① `showSymbol` 显式为真 → 全画；② **无类目轴**（数值轴）→ 全画；
  ③ `showAllSymbol:'auto'` 且 `canShowAllSymbolForCategory()` 通过 → 全画；
  ④ 都不满足才退到类目标签间隔策略。
  `LineSeries.ts:210` `showAllSymbol` 缺省 `'auto'`，`showSymbol:true`（:205）。
- Zan 只实现了 ④。数值轴折线、稀疏类目折线都会**少画点**。

### W3 · `axis.scale` 从不读取 → 紧致量程必被 0 撑开

- Zan `Chart.zan:1019`、`3006`：`AxisMinForF` / `AxisMaxForF` 的初值是
  `int mn = 0` / `int mx = 0` —— **无条件把 0 并入量程**。
- 引擎对 `"scale"` 这个键的访问器读取次数 = **0**。
- ECharts `coord/axisModelCommonMixin.ts:33`：`needIncludeZero() { return !scale; }`，
  在 `coord/scaleRawExtentInfo.ts:302-316` 只在 `needIncludeZero` 为真时并入 0。
- 影响：**28 个 demo** 显式设了轴级 `scale:true`（candlestick-sh / candlestick-touch /
  scatter-* / bubble-gradient / pictorialBar-hill …），它们的紧致包络在 Zan 侧
  一律被 0 撑爆；`scale:false` 的 demo 反而"碰巧是对的"。

### W4 · 278 个配置键被静默丢弃

引擎从不读取、但官方示例在用、且影响面最大的（完整表由脚本生成）：

| 键 | 文件数 | 语义 | 代表 demo |
|----|-------|------|-----------|
| `animation` / `animationDuration` / `animationEasing` / `animationDurationUpdate` / `animationEasingUpdate` | 33 / 17 / 9 / 25 / 13 | 动画时长与缓动 | bar-race、bar-animation-delay |
| `scale` | 28 | 轴紧致量程 | 见 W3 |
| `zlevel` / `z` | 10 / 26 | 图层序 | bar-gradient 等 |
| `padding` | 18 | label/tooltip 内边距 | bar-rich-text、candlestick-brush |
| `readOnly` | 16 | toolbox dataView 只读 | bar-label-rotation |
| `triggerOn` | 15 | tooltip 触发方式 | line-tooltip-touch |
| `nameGap` | 13 | 轴名与轴线的间距 | boxplot-* |
| `onZero` | 13 | 零基线开关 | area-rainfall |
| `labelLine` | 12 | 引导线（饼/漏斗/地图饼） | funnel、pie-borderRadius |
| `graphic` / `elements` | 11 / 8 | graphic 组件 | bar-race-country |
| `rich` | 11 | 富文本标签 | bar-rich-text |
| `levels` | 10 | 层级样式（treemap/sunburst） | sunburst-book |
| `realtimeSort` | 2（`realtime` 10） | bar-race 动态排序 | bar-race |
| `barMinHeight` | 1 文件（40 处 series） | 柱最小高度 | bar-* |
| `sampling` / `lttb` | 2（`lttb` 0） | 折线抽样 | area-simple |
| `universalTransition` | 8 | 跨图过渡 | bar-drilldown |
| `filterMode` | 7 | dataZoom 过滤语义 | custom-profile |
| `outOfRange` | 7 | visualMap 域外样式 | line-aqi |
| `labelLayout` | 7 | 标签防重叠 | graph-label-overlap |
| `minorTick` / `minorSplitLine` / `alignTicks` / `breaks` | 少 | v6 轴模型 | line-function、intraday-breaks-* |

> 注意：`gridIndex`（14 文件）是**假阳性**——转换器已同时输出
> `xAxisIndex/yAxisIndex`，引擎按数组位次绑定，`gridIndex` 冗余。这类
> 冗余键在 278 里占少数，逐条推进时需先看是否有等价键已读。

---

## 三、已核实的「缺失」（源里有，引擎完全没有）

| # | 功能 | ECharts 源 | 证据 | 影响 |
|---|------|-----------|------|------|
| A1 | **matrix 坐标系** | `coord/matrix/*`（~1972 行）+ `component/matrix/MatrixView.ts` | 引擎里 `matrix` 只是 **chord 的 n×n 权重阵**（`ChartModel.zan:1442,5036`），无 matrixCoord/DrawMatrix | 10 个 `ready:true` 的 matrix demo（白板） |
| A2 | **realtimeSort 动态排序柱** | `chart/bar/BarSeries.ts:97,111`、`BarView.ts:478-636` | `realtimeSort` 引擎 0 命中 | bar-race、bar-race-country |
| A3 | **custom series + renderItem** | `chart/custom/*` | `renderItem` 引擎 0 命中 | registry 已剔除 custom×21（a15d263c） |
| A4 | **graphic 组件** | `component/graphic/*` | `graphic` 11 处配置，访问器读取 0 | 8 文件，含 graphic-* 三例 |
| A5 | **aria（含 decal）** | `component/aria/*` | `aria` / `decal` 引擎 0 命中 | aria-* 、matrix-mbti |
| A6 | **thumbnail** | `component/thumbnail/*` | 0 命中 | graph-npm、graph-webkit-dep |
| A7 | **media 响应式查询** | `preprocessor`/`model` | `media` 4 处、读取 0 | data-transform-multiple-pie、matrix-grid-layout |
| A8 | **brush / dataZoomSelect** | `component/brush/*`、`dataZoomSelect.ts` | `brush` 1 处、读取 0 | 6 个 brush demo |
| A9 | **singleAxis 坐标系** | `coord/single/*` | `singleAxis` 1 处、读取 0 | themeRiver 两例 |
| A10 | **axis.breaks / minorTick / alignTicks** | `coord/axisNiceTicks.ts`、`scale/minorTicks.ts` | `breaks`/`minorTick` 0 命中 | intraday-breaks-*、line-function |

### 已纠正：这些**不是**缺失（旧账本/旧审计的过期项）

`treemap`、`sunburst`、`tree`、`pictorialBar`、`parallel`、`themeRiver`、
`graph/force`、`calendar`、`title`、`polar` **均已有渲染器并已接线**
（`ChartViewHier.zan` / `ChartViewPictorial.zan` / `ChartLayoutRelation.zan` /
`ChartViewCalendar.zan` / `ChartViewPolar.zan`；`ChartView.zan:1419-1489` 分派）。
`docs/archive/CHART_PORT_AUDIT_2026-09-10.md` 里「treemap/sunburst/pictorialBar/
parallel/graph 整类失效 100%」是**过期结论**——它测的是 2026-09-10 02:54 的
构建，早于 28207b3f / f4eae183 / 4aa39232 等提交。

---

## 四、旧口径的问题（为什么改走代码口径）

1. **截图审计的产物会腐烂。** `docs/archive/CHART_PORT_AUDIT_2026-09-10.md` 引用的
   `_scratch/shots/`（335 张 Zan 侧截图）**现在只剩 5 个文件**；
   `render_audit.json` 停在 2026-09-10 16:11，早于其后 5 个修复提交。
   账本引用的 `_scratch/echarts-master/`（596 个源文件）同样是 git-ignored，
   7 天清理策略一到，全部行号引用直接悬空。
2. **"能画出来"证明不了"画对了"。** 着色占比只能抓"白板"，
   抓不到 W1（柱宽公式错）、W2（少画符号）、W3（量程被撑开）——
   这三者的着色占比都"正常"。
3. **比例口径本身不稳。** 旧审计自己废弃过两套度量（墨迹网格、
   主色面积），最终方案仍有 131/335（39%）落到"不判定"。

代码口径的唯一缺点是**慢**（每条要读源），但它给出的是**可修的原因**，
而不是**待解释的现象**。

---

## 五、推进顺序（按"影响面 ÷ 修复成本"）

**P0 — 地基（改一处，全套受益）**
1. W1 柱宽/柱间距照抄 `barGrid.ts:calcBarWidthAndOffset()`（40 个 bar + 类目带宽）。
2. W3 `axis.scale` 语义 + `AxisMinForF/MaxForF` 初值改 ±Infinity，
   0 的并入交给 `needIncludeZero()`（28 个 demo 直接受益）。
3. W2 折线符号补 `getIsIgnoreFunc` 的数值轴 / `canShowAllSymbolForCategory` 提前返回。
4. A10 轴模型（`minorTick`/`alignTicks`/`breaks`）。

**P1 — 整类白板**
5. A1 matrix 坐标系（10 例）。
6. A2 realtimeSort（2 例）。
7. A4 graphic、A5 aria/decal、A6 thumbnail、A9 singleAxis。

**P2 — 局部键**
8. W4 表内逐条：`labelLine`、`rich`、`labelLayout`、`outOfRange`、
   `filterMode`、`nameGap`、`onZero`、`padding`、动画族。

**纪律**
- 每修一条，必须在 Zan 侧注释写上 ECharts 源 `文件:行`，否则这条无法审计。
- 每修一条，同步删掉本文对应行；**账本不留已完成项**。
- 不新增"看起来像"的近似；近似要么记进本文当缺口，要么改成照抄。

---

## 六、验证机制：两个不会腐烂的闸门

截图口径之所以失效，是因为"判据"和"证据"都是像素，既不自证也不留存。
补上两个**数值**闸门，正确性才有可执行的判据。

### 闸门 1 · 数值 oracle（`scripts/ec_oracle.js`）

**真的 ECharts 引擎可以在 node 里无头跑**：`dist/echarts.js` +
`init(null,null,{renderer:'svg',ssr:true,width,height})` +
`getZr().storage.getDisplayList(true)`，直接读出每个图元的**精确浮点
几何**（`rect` 的 `x/y/width/height`、`circle` 的 `cx/cy/r`、`line` 的
两端点），以及轴刻度文本。不需要解析 SVG，不需要窗口，不依赖时钟。

```bash
node scripts/ec_oracle.js --option examples/gui_charts/options/bar-simple.json \
     --width 400 --height 300
```

实例（同一个 option，官方 vs 现引擎）：

| 量 | 官方 oracle | 现引擎公式 |
|----|-----------|-----------|
| `bar-simple`（7 类目 1 系列，band=42.857） | 柱宽 **29.571**、首柱 x **66.643** | `groupW=step*7/10=30.0`，首柱 x ≈ 66.43 |
| 4 类目 2 系列（band=75） | 柱宽 **26.071**、第二系列 x **98.804** | `barW=(52.5-2)/2=25.25`、x ≈ 98.8± |

官方数由 `barGrid.ts:255-270` 精确复现：`barCategoryGap=max(35-2*4,15)%=27%`、
`autoWidth=(75-20.25)/(2+0.1)=26.071`、组内起始偏移 `catgap/2=10.125`。

**用法**：每个要移植的函数，先用 oracle 对一组固定输入取数，落成
golden JSON；Zan 侧移植后必须逐值一致（浮点按 1e-3 比）。这把
"像不像"换成了"等不等"。

参照源码固定为 **6.1.0 / git `33ec5201159fe84f1317e4a7b00d63a73e095703`**；
本机 `_scratch/echarts-master/` 有 README 保护、不被 7 天清理扫走，也可
`git clone` 后 checkout 该 sha。

### 闸门 2 · 未读配置键棘轮（`scripts/chart_gap_audit.py --check`）

278 个未读键是**存量债**，靠人工记；棘轮保证它只减不增：

```bash
python scripts/chart_gap_audit.py --check docs/chart_gap_baseline.json
```

- 出现 baseline 之外的新未读键 → 退出码 1（新增 demo / 改 option 时
  静默丢配置会当场被拦）。
- Zan 侧**源引用丢失**同样报错——某个函数的 `文件:行` 出处被删掉，
  等于这条移植失去审计能力，与新增缺口同级。
- 实现一个键后重新 `--write-baseline`，账本同步删行。

## 七、复现

```bash
python scripts/chart_gap_audit.py                        # 汇总 + 未读键倒排
python scripts/chart_gap_audit.py --check docs/chart_gap_baseline.json
python scripts/chart_gap_audit.py --write-baseline docs/chart_gap_baseline.json
python scripts/chart_gap_audit.py --markdown out.md --json out.json
node scripts/ec_oracle.js --option <option.json> [--width W] [--height H]
```

`chart_gap_audit.py` 无外部依赖（只读仓库内
`examples/gui_charts/options` 与 `stdlib/Gui/Component/Chart`）。
`ec_oracle.js` 需要 node + ECharts 6.1 dist（见闸门 1），是开发工具，
不进构建、不进 ctest 档位。
